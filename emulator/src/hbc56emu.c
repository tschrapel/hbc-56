/*
 * Troy's HBC-56 Emulator
 *
 * Copyright (c) 2021 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/hbc-56/emulator
 *
 */


#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

#include "window.h"
#include "cpu6502.h"
 //#include "tms9918_core.h"
#include "emu2149.h"
#include "debugger.h"
#include "keyboard.h"
#include "lcd.h"
#include "ram_device.h"
#include "tms9918_device.h"

#define HAVE_THREADS 0

//#define NO_THREADS 1

#ifndef _MAX_PATH 
#define _MAX_PATH 256
#endif

char winTitleBuffer[_MAX_PATH];

#define NUM_DEVICES 12
HBC56Device* devices[NUM_DEVICES];


uint16_t ioPage = 0x7f00;

//#define TMS9918_FPS 60
#define TMS9918_DAT_ADDR 0x10
#define TMS9918_REG_ADDR 0x11

// currently keyboard and NES use the same port (I haven't built separate hardware... yet)
// NES controller is the default.  Use the --keyboard command-line to enable the keyboard instead
#define NES_IO_PORT 0x81  
#define KB_IO_PORT 0x81

#define LCD_IO_PORT 0x02

#define LCD_IO_CMD   LCD_IO_PORT
#define LCD_IO_DATA  (LCD_IO_CMD | 0x01)


SDL_AudioDeviceID audioDevice;


#define NES_RIGHT  0b10000000
#define NES_LEFT   0b01000000
#define NES_DOWN   0b00100000
#define NES_UP     0b00010000
#define NES_START  0b00001000
#define NES_SELECT 0b00000100
#define NES_B      0b00000010
#define NES_A      0b00000001

//VrEmuTms9918a *tms9918 = NULL;
PSG* psg0 = NULL;
PSG* psg1 = NULL;
LCDWindow* lcdw = NULL;

SDL_mutex* tmsMutex = NULL;
SDL_mutex* ayMutex = NULL;
SDL_mutex* debugMutex = NULL;

char kbQueue[16] = { 0 };
int kbStart = 0, kbEnd = 0;


#define AY3891X_IO_ADDR 0x40

#define AY3891X_PSG0 0x00
#define AY3891X_PSG1 0x04

#define AY3891X_S0 (AY3891X_IO_ADDR | AY3891X_PSG0)
#define AY3891X_S1 (AY3891X_IO_ADDR | AY3891X_PSG1)

#define AY3891X_INACTIVE 0x03
#define AY3891X_READ     0x02
#define AY3891X_WRITE    0x01
#define AY3891X_ADDR     0x00

float audioBuffer[500000];

byte psg0Addr = 0;
byte psg1Addr = 0;

byte kbReadCount = 0;
int keyboardMode = 0;

uint8_t io_read(uint8_t addr, int dbg)
{
  uint8_t val = 0;

  switch (addr)
  {
  case LCD_IO_CMD:
    if (lcdw && lcdw->lcd) val = vrEmuLcdReadAddress(lcdw->lcd);
    break;

  case LCD_IO_DATA:
    if (lcdw && lcdw->lcd) val = vrEmuLcdReadByte(lcdw->lcd);
    break;

  case NES_IO_PORT:  /* same as KB_IO_PORT */
    if (keyboardMode)
    {
      if (kbEnd != kbStart)
      {
        val = kbQueue[kbStart];

        if (++kbReadCount & 0x01)
        {
          ++kbStart;
          kbStart &= 0x0f;
        }

      }
    }
    else
    {
      const Uint8* keystate = SDL_GetKeyboardState(NULL);
      int isNumLockOff = (SDL_GetModState() & KMOD_NUM) == 0;

      //continuous-response keys
      if (keystate[SDL_SCANCODE_LEFT] || (keystate[SDL_SCANCODE_KP_4] && isNumLockOff))
      {
        val |= NES_LEFT;
      }
      if (keystate[SDL_SCANCODE_RIGHT] || (keystate[SDL_SCANCODE_KP_6] && isNumLockOff))
      {
        val |= NES_RIGHT;
      }
      if (keystate[SDL_SCANCODE_UP] || (keystate[SDL_SCANCODE_KP_8] && isNumLockOff))
      {
        val |= NES_UP;
      }
      if (keystate[SDL_SCANCODE_DOWN] || (keystate[SDL_SCANCODE_KP_2] && isNumLockOff))
      {
        val |= NES_DOWN;
      }
      if (keystate[SDL_SCANCODE_LCTRL] || keystate[SDL_SCANCODE_RCTRL] || keystate[SDL_SCANCODE_B])
      {
        val |= NES_B;
      }
      if (keystate[SDL_SCANCODE_LSHIFT] || keystate[SDL_SCANCODE_RSHIFT] || keystate[SDL_SCANCODE_A])
      {
        val |= NES_A;
      }
      if (keystate[SDL_SCANCODE_TAB])
      {
        val |= NES_SELECT;
      }
      if (keystate[SDL_SCANCODE_SPACE] || keystate[SDL_SCANCODE_RETURN])
      {
        val |= NES_START;
      }

      val = ~val;
    }
    break;

  case (AY3891X_S0 | AY3891X_READ):
    if (SDL_LockMutex(ayMutex) == 0)
    {
      val = PSG_readReg(psg0, psg0Addr);
      SDL_UnlockMutex(ayMutex);
    }
    break;

  case (AY3891X_S1 | AY3891X_READ):
    if (SDL_LockMutex(ayMutex) == 0)
    {
      val = PSG_readReg(psg1, psg1Addr);
      SDL_UnlockMutex(ayMutex);
    }
    break;
  }

  return val;
}

void io_write(uint8_t addr, uint8_t val)
{
  switch (addr)
  {
  case LCD_IO_CMD:
    if (lcdw && lcdw->lcd) vrEmuLcdSendCommand(lcdw->lcd, val);
    break;

  case LCD_IO_DATA:
    if (lcdw && lcdw->lcd) vrEmuLcdWriteByte(lcdw->lcd, val);
    break;

  case (AY3891X_S0 | AY3891X_ADDR):
    psg0Addr = val;
    break;

  case (AY3891X_S0 | AY3891X_WRITE):
    if (SDL_LockMutex(ayMutex) == 0)
    {
      PSG_writeReg(psg0, psg0Addr, val);
      SDL_UnlockMutex(ayMutex);
    }
    break;

  case (AY3891X_S1 | AY3891X_ADDR):
    psg1Addr = val;
    break;

  case (AY3891X_S1 | AY3891X_WRITE):
    if (SDL_LockMutex(ayMutex) == 0)
    {
      PSG_writeReg(psg1, psg1Addr, val);
      SDL_UnlockMutex(ayMutex);
    }
    break;

  }

}

uint8_t mem_read_impl(uint16_t addr, int dbg)
{
  uint8_t val = 0xff;

  int status = 0;

  for (size_t i = 0; i < NUM_DEVICES; ++i)
  {
    if (!devices[i]) break;
    if (devices[i]->readFn)
    {
      if (status = devices[i]->readFn(devices[i], addr, &val, dbg)) break;
    }
  }

  if (!status)
  {
    if ((addr & 0xff00) == ioPage)
    {
      val = io_read(addr & 0xff, dbg);
    }
  }

  return val;
}
uint8_t mem_read(uint16_t addr) {
  return mem_read_impl(addr, 0);
}
uint8_t mem_read_dbg(uint16_t addr) {
  return mem_read_impl(addr, 1);
}


void mem_write(uint16_t addr, uint8_t val)
{
  int status = 0;
  for (size_t i = 0; i < NUM_DEVICES; ++i)
  {
    if (!devices[i]) break;
    if (devices[i]->writeFn)
    {
      if (status = devices[i]->writeFn(devices[i], addr, val)) break;
    }
  }

  if (!status)
  {
    if ((addr & 0xff00) == ioPage)
    {
      io_write(addr & 0xff, val);
    }
  }
}


static SDLCommonState* state;

int done;
int completedWarmUpSamples = 0;

void hbc56AudioCallback(
  void* userdata,
  Uint8* stream,
  int    len)
{
  const int warmUpSamples = 20000;

  int samples = len / (sizeof(float) * 2);
  float* str = (float*)stream;

  static float lastSample0 = 0.0f;
  static float lastSample1 = 0.0f;

  if (SDL_LockMutex(ayMutex) == 0)
  {
    for (int i = 0; i < samples; ++i)
    {
      PSG_calc(psg0);
      PSG_calc(psg1);

      // slowly bring output to resting level
      if (!done && completedWarmUpSamples < warmUpSamples)
      {
        str[i * 2] = -0.5f * ((float)completedWarmUpSamples / (float)warmUpSamples);
        str[i * 2 + 1] = -0.5f * ((float)completedWarmUpSamples / (float)warmUpSamples);
        lastSample0 = str[i * 2];
        lastSample1 = str[i * 2 + 1];
        ++completedWarmUpSamples;
        continue;
      }
      // slowly bring output to zero
      else if (done)
      {
        str[i * 2] = lastSample0 * 0.9995f;
        str[i * 2 + 1] = lastSample1 * 0.9995f;
        lastSample0 = str[i * 2];
        lastSample1 = str[i * 2 + 1];
        if (completedWarmUpSamples > 0) completedWarmUpSamples -= 1;
        continue;
      }

      int16_t l = psg0->ch_out[0] + psg1->ch_out[0] + (psg0->ch_out[2] + psg1->ch_out[2]);
      int16_t r = psg0->ch_out[1] + psg1->ch_out[1] + (psg0->ch_out[2] + psg1->ch_out[2]);

      // convert the range 0.0 -> 8192 * 4 to -1.0 to 1.0.
      float thisSample0 = ((float)l / (8192.0f * 4)) - 0.5f;
      float thisSample1 = ((float)r / (8192.0f * 4)) - 0.5f;

      str[i * 2] = thisSample0 * 0.2f + lastSample0 * 0.8f;
      str[i * 2 + 1] = thisSample1 * 0.2f + lastSample1 * 0.8f;

      lastSample0 = str[i * 2];
      lastSample1 = str[i * 2 + 1];
    }
    SDL_UnlockMutex(ayMutex);
  }
}

Uint32 lastRenderTicks = 0;
/*
byte lineBuffer[TMS9918A_PIXELS_X];

Uint8 tms9918Reds[]   = {0x00, 0x00, 0x21, 0x5E, 0x54, 0x7D, 0xD3, 0x43, 0xFd, 0xFF, 0xD3, 0xE5, 0x21, 0xC9, 0xCC, 0xFF};
Uint8 tms9918Greens[] = {0x00, 0x00, 0xC9, 0xDC, 0x55, 0x75, 0x52, 0xEB, 0x55, 0x79, 0xC1, 0xCE, 0xB0, 0x5B, 0xCC, 0xFF};
Uint8 tms9918Blues[]  = {0x00, 0x00, 0x42, 0x78, 0xED, 0xFC, 0x4D, 0xF6, 0x54, 0x78, 0x53, 0x80, 0x3C, 0xBA, 0xCC, 0xFF};
*/
int callCount = 0;
double perfFreq = 0.0;
double currentFreq = 0.0;
int triggerIrq = 0;

Uint32 lastSecond = 0;

#define LOGICAL_DISPLAY_SIZE_X 320
#define LOGICAL_DISPLAY_SIZE_Y 240
#define LOGICAL_DISPLAY_BPP    3

#define LOGICAL_WINDOW_SIZE_X (LOGICAL_DISPLAY_SIZE_X * 2)
#define LOGICAL_WINDOW_SIZE_Y (LOGICAL_DISPLAY_SIZE_Y * 1.5)


#define TMS_OFFSET_X ((LOGICAL_DISPLAY_SIZE_X - TMS9918A_PIXELS_X) / 2)
#define TMS_OFFSET_Y ((LOGICAL_DISPLAY_SIZE_Y - TMS9918A_PIXELS_Y) / 2)

byte frameBuffer[LOGICAL_DISPLAY_SIZE_X * LOGICAL_DISPLAY_SIZE_Y * LOGICAL_DISPLAY_BPP];
byte debugFrameBuffer[DEBUGGER_WIDTH_PX * DEBUGGER_HEIGHT_PX * LOGICAL_DISPLAY_BPP];
SDL_Texture* debugWindowTex = NULL;
int debugWindowShown = 1;
int debugStep = 0;
int debugStepOver = 0;
int debugPaused = 0;
int debugStepOut = 0;
uint16_t callStack[128] = { 0 };
int callStackPtr = 0;

#define CLOCK_FREQ 4000000

#if HAVE_THREADS

int SDLCALL cpuThread(void* unused)
{
#endif
  double ticksPerClock = 1.0 / (double)CLOCK_FREQ;

  double lastTime = 0.0;
  double thisLoopStartTime = 0;
  double initialLastTime = 0;
  uint16_t breakPc = 0;

#if !HAVE_THREADS
  void cpuTick()
  {
#else
  while (1)
  {
#endif
    if (lastTime == 0.0)
    {
      lastTime = (double)SDL_GetPerformanceCounter() / perfFreq;
    }

    double currentTime = (double)SDL_GetPerformanceCounter() / perfFreq;
    Uint64 thisLoopTicks = 0;
    initialLastTime = lastTime;
    while (lastTime < currentTime)
    {
      if (triggerIrq && !debugPaused)
      {
        cpu6502_irq();
        triggerIrq = 0;
      }

      if (SDL_LockMutex(debugMutex) == 0)
      {
        uint8_t opcode = mem_read(cpu6502_get_regs()->pc);
        int isJsr = (opcode == 0x20);
        int isRts = (opcode == 0x60);

        if (debugStepOver && !breakPc)
        {
          if (isJsr)
          {
            breakPc = cpu6502_get_regs()->pc + 3;
          }
          debugStepOver = 0;
        }

        if (!debugPaused || debugStep || breakPc)
        {
          thisLoopTicks += cpu6502_single_step();
          debugStep = 0;
          if (cpu6502_get_regs()->pc == breakPc)
          {
            breakPc = 0;
          }
          else if (cpu6502_get_regs()->lastOpcode == 0xDB)
          {
            debugPaused = debugWindowShown = 1;
          }
        }

        SDL_UnlockMutex(debugMutex);
      }
      lastTime = initialLastTime + (thisLoopTicks * ticksPerClock);
#if !HAVE_THREADS
      if (debugPaused) break;
#endif
    }

    //SDL_Delay(1);

    double tmpFreq = (double)SDL_GetPerformanceCounter() / perfFreq - currentTime;
    currentFreq = currentFreq * 0.9 + (((double)thisLoopTicks / tmpFreq) / 1000000.0) * 0.1;
  }
#if HAVE_THREADS
  return 0;
  }
#endif

void hbc56Reset()
{
  kbStart = 0;
  kbEnd = 0;
  debugPaused = 0;

  cpu6502_rst();
  PSG_reset(psg0);
  PSG_reset(psg1);
}


double mainLoopLastTime = 0.0;


void doTick()
{
  double thisTime = (double)SDL_GetPerformanceCounter() / perfFreq;


  for (size_t i = 0; i < NUM_DEVICES; ++i)
  {
    if (!devices[i]) break;
    if (devices[i]->tickFn)
    {
      devices[i]->tickFn(devices[i], 100, thisTime - mainLoopLastTime);
    }
  }

  mainLoopLastTime = thisTime;
}

void
loop()
{
  int i;
  SDL_Event event;

#if !HAVE_THREADS
  cpuTick();
#endif

  doTick();

  SDL_Rect dest;
  dest.x = 0;
  dest.y = 0;
  dest.w = (int)(LOGICAL_DISPLAY_SIZE_X * 3);
  dest.h = (int)(LOGICAL_WINDOW_SIZE_Y * 2);


  Uint32 currentTicks = SDL_GetTicks();
  if ((currentTicks - lastRenderTicks) > 16)
  {
    lastRenderTicks = currentTicks;

    for (size_t i = 0; i < NUM_DEVICES; ++i)
    {
      if (!devices[i]) break;
      if (devices[i]->renderFn)
      {
        devices[i]->renderFn(devices[i]);
      }
      if (devices[i]->output)
      {
        SDL_RenderCopy(state->renderers[0], devices[i]->output, NULL, &dest);
      }
    }

    hbc56AudioCallback(NULL, audioBuffer, 44100 * sizeof(float) * 2);
    SDL_QueueAudio(audioDevice, audioBuffer, 44100 * sizeof(float) * 2);

//    SDL_RenderPresent(state->renderers[0]);
  }
  else
  {
    SDL_Delay(1);
    return;
  }

  if (lcdw)
  {
    lcdWindowUpdate(lcdw);

    int newWidth = lcdw->pixelsWidth / 2;
    int newHeight = lcdw->pixelsHeight / 2;

    SDL_Rect dest2 = dest;
    dest2.y = (dest.h - newHeight) / 2;
    dest2.h = newHeight;
    dest2.x = dest.x + (dest.w - newWidth) / 2;
    dest2.w = newWidth;

    SDL_RenderCopy(lcdw->renderer, lcdw->tex, NULL, &dest2);
  }

  if (debugWindowShown)
  {
    for (int i = 0; i < sizeof(debugFrameBuffer); ++i)
    {
      debugFrameBuffer[i] = i & 0xff;
    }

    int mouseX, mouseY;
    SDL_GetMouseState(&mouseX, &mouseY);

    int winSizeX, winSizeY;
    SDL_GetWindowSize(state->windows[0], &winSizeX, &winSizeY);

    double factorX = winSizeX / (double)DEFAULT_WINDOW_WIDTH;
    double factorY = winSizeY / (double)DEFAULT_WINDOW_HEIGHT;

    mouseX = (int)(mouseX / factorX);
    mouseY = (int)(mouseY / factorY);
    mouseX -= dest.w * 2;

    debuggerUpdate(debugWindowTex, mouseX, mouseY);
    dest.x = dest.w;
    dest.w = (int)(DEBUGGER_WIDTH_PX);
    dest.h = (int)(DEBUGGER_HEIGHT_PX);
    SDL_RenderCopy(state->renderers[0], debugWindowTex, NULL, &dest);
  }


  SDL_RenderPresent(state->renderers[0]);
  
  while (SDL_PollEvent(&event)) {
    switch (event.type) {
    case SDL_KEYDOWN:
    {
      SDL_bool withControl = (event.key.keysym.mod & KMOD_CTRL) ? 1 : 0;

      if (event.key.keysym.sym == SDLK_F5) break;
      if (event.key.keysym.sym == SDLK_F10) break;
      if (event.key.keysym.sym == SDLK_F11) break;
      if (event.key.keysym.sym == SDLK_F12) break;
      if (event.key.keysym.sym == SDLK_ESCAPE) break;
      if (withControl && event.key.keysym.sym == SDLK_r)
      {
        break;
      }

      uint64_t ps2ScanCode = sdl2ps2map[event.key.keysym.scancode][0];
      for (int i = 0; i < 8; ++i)
      {
        uint8_t scanCodeByte = (ps2ScanCode & 0xff00000000000000) >> 56;
        if (scanCodeByte)
        {
          kbQueue[kbEnd++] = scanCodeByte; kbEnd &= 0x0f;
        }
        ps2ScanCode <<= 8;
      }
    }
    break;

    case SDL_KEYUP:
    {
      SDL_bool withControl = (event.key.keysym.mod & KMOD_CTRL) ? 1 : 0;
      if (event.key.keysym.sym == SDLK_F5) break;
      if (event.key.keysym.sym == SDLK_F10) break;
      if (event.key.keysym.sym == SDLK_F11) break;
      if (event.key.keysym.sym == SDLK_F12) break;
      if (event.key.keysym.sym == SDLK_ESCAPE) break;
      if (withControl && event.key.keysym.sym == SDLK_r)
      {
        hbc56Reset();
        break;
      }
      if (event.key.keysym.sym == SDLK_LCTRL) break;
      if (event.key.keysym.sym == SDLK_RCTRL) break;

      uint64_t ps2ScanCode = sdl2ps2map[event.key.keysym.scancode][1];
      for (int i = 0; i < 8; ++i)
      {
        uint8_t scanCodeByte = (ps2ScanCode & 0xff00000000000000) >> 56;
        if (scanCodeByte)
        {
          kbQueue[kbEnd++] = scanCodeByte; kbEnd &= 0x0f;
        }
        ps2ScanCode <<= 8;
      }
    }
    break;
    }


    SDLCommonEvent(state, &event, &done);
  }

#if 0
  if (currentSecond != lastSecond)
  {
    char tempTitleBuffer[_MAX_PATH];
    SDL_snprintf(tempTitleBuffer, sizeof(tempTitleBuffer), "%s (%.3f MHz)", winTitleBuffer, currentFreq);
    for (i = 0; i < state->num_windows; ++i)
      SDL_SetWindowTitle(state->windows[i], tempTitleBuffer);

    lastSecond = currentSecond;
  }
#endif

#ifdef __EMSCRIPTEN__
  if (done) {
    emscripten_cancel_main_loop();
  }
#endif
}

char labelMapFile[FILENAME_MAX] = { 0 };


int loadRom(const char* filename)
{
  FILE* ptr = NULL;
  int romLoaded = 0;

#ifdef _EMSCRIPTEN
  ptr = fopen(filename, "rb");
#else
  fopen_s(&ptr, filename, "rb");
#endif

  SDL_snprintf(winTitleBuffer, sizeof(winTitleBuffer), "Troy's HBC-56 Emulator - %s", filename);

  if (ptr)
  {
    byte rom[0x8000];
    size_t romBytesRead = fread(rom, 1, sizeof(rom), ptr);
    fclose(ptr);

    if (romBytesRead != sizeof(rom))
    {
#ifndef _EMSCRIPTEN
      SDL_snprintf(winTitleBuffer, sizeof(winTitleBuffer), "Error. ROM file '%s' must be %d bytes.", filename, (int)sizeof(rom));
      SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Troy's HBC-56 Emulator", winTitleBuffer, NULL);
#endif
    }
    else
    {
      romLoaded = 1;

      devices[0] = createRomDevice(0x8000, 0xffff, rom);

      SDL_strlcpy(labelMapFile, filename, FILENAME_MAX);
      size_t ln = SDL_strlen(labelMapFile);
      SDL_strlcpy(labelMapFile + ln, ".lmap", FILENAME_MAX - ln);
    }
  }
  else
  {
#ifndef _EMSCRIPTEN
    SDL_snprintf(winTitleBuffer, sizeof(winTitleBuffer), "Error. ROM file '%s' does not exist.", filename);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Troy's HBC-56 Emulator", winTitleBuffer, NULL);
#endif
    return 2;
  }

  return romLoaded;
}


int
main(int argc, char* argv[])
{
  int i;
  Uint32 then, now, frames;

  for (size_t i = 0; i < NUM_DEVICES; ++i)
  {
    devices[i] = NULL;
  }

  perfFreq = (double)SDL_GetPerformanceFrequency();

  /* Enable standard application logging */
  SDL_LogSetPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO);

  SDL_snprintf(winTitleBuffer, sizeof(winTitleBuffer), "Troy's HBC-56 Emulator");

  /* Initialize test framework */
  state = SDLCommonCreateState(argv, SDL_INIT_VIDEO | SDL_INIT_AUDIO);
  if (!state) {
    return 1;
  }
  int romLoaded = 0;
  LCDType lcdType = LCD_NONE;

#if _EMSCRIPTEN
  romLoaded = loadRom("rom.bin");
  keyboardMode = 1;
  lcdType = LCD_GRAPHICS;
#endif

  for (i = 1; i < argc;) {
    int consumed;

    consumed = SDLCommonArg(state, i);
    if (consumed <= 0) {
      consumed = -1;
      if (SDL_strcasecmp(argv[i], "--rom") == 0) {
        if (argv[i + 1]) {
          consumed = 1;
          romLoaded = loadRom(argv[++i]);
        }
      }
      /* start paused? */
      else if (SDL_strcasecmp(argv[i], "--brk") == 0)
      {
        consumed = 1;
        debugPaused = 1;
      }
      /* use keyboard instead of NES controller */
      else if (SDL_strcasecmp(argv[i], "--keyboard") == 0)
      {
        consumed = 1;
        keyboardMode = 1;
      }
      /* enable the lcd? */
      else if (SDL_strcasecmp(argv[i], "--lcd") == 0)
      {
        if (argv[i + 1])
        {
          consumed = 1;
          switch (atoi(argv[i + 1]))
          {
          case 1602:
            lcdType = LCD_1602;
            break;
          case 2004:
            lcdType = LCD_2004;
            break;
          case 12864:
            lcdType = LCD_GRAPHICS;
            break;
          }
          ++i;
        }
      }
    }
    if (consumed < 0) {
      static const char* options[] = { "--rom <romfile>","[--brk]","[--keyboard]", NULL };
      SDLCommonLogUsage(state, argv[0], options);
      return 2;
    }
    i += consumed;
  }

  if (romLoaded == 0) {
    static const char* options[] = { "--rom <romfile>","[--brk]","[--keyboard]","[--lcd 1602|2004|12864]", NULL };
    SDLCommonLogUsage(state, argv[0], options);

#ifndef _EMSCRIPTEN
    SDL_snprintf(winTitleBuffer, sizeof(winTitleBuffer), "No HBC-56 ROM file.\n\nUse --rom <romfile>");
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Troy's HBC-56 Emulator", winTitleBuffer, NULL);
#endif

    return 2;
  }

  state->window_title = winTitleBuffer;

  if (!SDLCommonInit(state)) {
    return 2;
  }

  devices[1] = createRamDevice(0x0000, 0x7eff);
  devices[2] = createTms9918Device(ioPage | TMS9918_DAT_ADDR, ioPage | TMS9918_REG_ADDR, state->renderers[0]);


  /* Create the windows and initialize the renderers */
  for (i = 0; i < state->num_windows; ++i) {
    SDL_Renderer* renderer = state->renderers[i];
    SDL_RenderClear(renderer);
    debugWindowTex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, DEBUGGER_WIDTH_PX, DEBUGGER_HEIGHT_PX);
    memset(frameBuffer, 0, sizeof(frameBuffer));

#ifndef _EMSCRIPTEN
    SDL_SetTextureScaleMode(debugWindowTex, SDL_ScaleModeBest);
#endif

  }


  tmsMutex = SDL_CreateMutex();
  ayMutex = SDL_CreateMutex();
  debugMutex = SDL_CreateMutex();

  SDL_AudioSpec want, have;

  SDL_memset(&want, 0, sizeof(want));
  want.freq = 44100;
  want.format = AUDIO_F32SYS;
  want.channels = 2;
  want.samples = want.freq / 60;
  ;;want.callback = hbc56AudioCallback;
  audioDevice = SDL_OpenAudioDevice(NULL, 0, &want, &have, SDL_AUDIO_ALLOW_ANY_CHANGE);

  srand((unsigned int)time(NULL));

  //tms9918 = vrEmuTms9918aNew();


  psg0 = PSG_new(2000000, have.freq);
  psg1 = PSG_new(2000000, have.freq);

#if HAVE_THREADS
  SDL_CreateThread(cpuThread, "CPU", NULL);
#endif

  /* Main render loop */
  frames = 0;
  then = SDL_GetTicks();
  done = 0;

  hbc56Reset();

  debuggerInit(cpu6502_get_regs(), labelMapFile, NULL);//tms9918);

  SDL_PauseAudioDevice(audioDevice, 0);

  lcdw = lcdWindowCreate(lcdType, state->windows[0], state->renderers[0]);

  //  SDL_CreateWindow("Debugger", 50, 50, 320, 200, 0);


#ifdef _EMSCRIPTEN
  emscripten_set_main_loop(loop, 0, 1);
#else
  while (!done) {
    ++frames;
    loop();
  }
#endif

  // cool down audio
  SDL_Delay(250);

  SDL_PauseAudio(1);
  SDL_CloseAudio();
  SDL_AudioQuit();

  //vrEmuTms9918aDestroy(tms9918);
  //tms9918 = NULL;

  SDL_DestroyMutex(tmsMutex);
  tmsMutex = NULL;

  lcdWindowDestroy(lcdw);
  lcdw = NULL;

  SDLCommonQuit(state);


  /* Print out some timing information */
  now = SDL_GetTicks();
  if (now > then) {
    double fps = ((double)frames * 1000) / (now - then);
    SDL_Log("%2.2f frames per second\n", fps);
  }
  return 0;
}

/* vi: set ts=4 sw=4 expandtab: */
