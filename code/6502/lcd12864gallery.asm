!cpu 6502
!initmem $FF
!to "lcd12864gallery.o", plain

*=$8000

LCD_CMD       = $7f00
LCD_DATA      = $7f01

LCD_CMD_CLEAR                = %00000001
LCD_CMD_HOME                 = %00000010

LCD_CMD_ENTRY_MODE           = %00000100
LCD_CMD_ENTRY_MODE_INCREMENT = %00000010
LCD_CMD_ENTRY_MODE_DECREMENT = %00000000
LCD_CMD_ENTRY_MODE_SHIFT     = %00000001

LCD_CMD_DISPLAY              = %00001000
LCD_CMD_DISPLAY_ON           = %00000100
LCD_CMD_DISPLAY_CURSOR       = %00000010
LCD_CMD_DISPLAY_CURSOR_BLINK = %00000001

LCD_CMD_SHIFT                = %00010000
LCD_CMD_SHIFT_CURSOR         = %00000000
LCD_CMD_SHIFT_DISPLAY        = %00001000
LCD_CMD_SHIFT_LEFT           = %00000000
LCD_CMD_SHIFT_RIGHT          = %00000100

LCD_CMD_SET_CGRAM_ADDR       = $40
LCD_CMD_SET_DRAM_ADDR        = $80

LCD_CMD_FUNCTIONSET     = $20
LCD_CMD_8BITMODE        = $10
LCD_CMD_2LINE           = $08
LCD_CMD_12864B_EXTENDED = $04

LCD_CMD_EXT_GRAPHICS_ENABLE  = $02

LCD_CMD_EXT_GRAPHICS_ADDR    = $80

LCD_INITIALIZE      = LCD_CMD_FUNCTIONSET | LCD_CMD_8BITMODE | LCD_CMD_2LINE
LCD_BASIC           = LCD_INITIALIZE
LCD_EXTENDED        = LCD_INITIALIZE | LCD_CMD_12864B_EXTENDED

DISPLAY_MODE  = <(LCD_CMD_DISPLAY | LCD_CMD_DISPLAY_ON) ; | LCD_CMD_DISPLAY_CURSOR | LCD_CMD_DISPLAY_CURSOR_BLINK)

STR_ADDR = $10
STR_ADDR_L = STR_ADDR
STR_ADDR_H = STR_ADDR + 1

LCD_ADDR_LINE1 = 0x00
LCD_ADDR_LINE2 = 0x10
LCD_ADDR_LINE3 = 0x08
LCD_ADDR_LINE4 = 0x18

CHARS_WIDTH = 16

	jsr lcdWait
	lda #LCD_INITIALIZE
	sta LCD_CMD

	jsr lcdWait
	lda #LCD_CMD_CLEAR
	sta LCD_CMD

	jsr lcdWait
	lda #LCD_CMD_HOME
	sta LCD_CMD

	jsr lcdWait
	lda #LCD_EXTENDED
	sta LCD_CMD

	jsr lcdWait
	lda #LCD_EXTENDED | LCD_CMD_EXT_GRAPHICS_ENABLE
	sta LCD_CMD

start:

	PIX_ADDR   = $20
	PIX_ADDR_L = PIX_ADDR
	PIX_ADDR_H = PIX_ADDR + 1
	
	IMG_ADDR_H   = $30
	

	lda #0
	sta PIX_ADDR_L
	


mainLoop:
	lda #>LOGO_IMG
	sta IMG_ADDR_H
	jsr outputImage
	
	jsr longDelay

	lda #>ROX_IMG
	sta IMG_ADDR_H
	jsr outputImage
	
	jsr longDelay
	
	lda #>LIV_IMG
	sta IMG_ADDR_H
	jsr outputImage
	
	jsr longDelay

	lda #>SELFIE_IMG
	sta IMG_ADDR_H
	jsr outputImage
	
	jsr longDelay
	
	jmp mainLoop


	
	
; Image. IMG_ADDR_H contains high byte of image data address
outputImage:
	ldy #0
	ldx #0

.imageLoop:
	lda IMG_ADDR_H
	sta PIX_ADDR_H

	; set y address
	jsr lcdWait
	tya
	ora #LCD_CMD_EXT_GRAPHICS_ADDR
	sta LCD_CMD

	; set x address
	jsr lcdWait
	txa
	ora #LCD_CMD_EXT_GRAPHICS_ADDR
	sta LCD_CMD

	; first byte
	jsr lcdWait

	txa
	pha
	tya
	pha

	cpx #8
	bcs +
	; upper half

	cpy #16
	bcc ++
	inc PIX_ADDR_H
	jmp ++

+

	; lower half
	inc PIX_ADDR_H
	inc PIX_ADDR_H

	cpy #16
	bcc ++
	inc PIX_ADDR_H

++

	tya
	and #$0f
	asl
	asl
	asl
	asl
	pha
	txa
	and #$07
	asl
	sta $02
	pla
	ora $02
	tay


	lda (PIX_ADDR), y
	;lda #0
	sta LCD_DATA

	; second byte
	jsr lcdWait

	iny
	lda (PIX_ADDR), y
	;lda #0
	sta LCD_DATA

	pla
	tay
	pla
	tax


	inx
	cpx #16
	bne .imageLoop
	ldx #0
	iny
	cpy #32
	bne .imageLoop

	rts


lcdLineOne:
	lda #(LCD_CMD_SET_DRAM_ADDR | LCD_ADDR_LINE1)
	sta LCD_CMD
	rts

lcdLineTwo:
	lda #(LCD_CMD_SET_DRAM_ADDR | LCD_ADDR_LINE2)
	sta LCD_CMD
	rts
	
lcdLineThree:
	lda #(LCD_CMD_SET_DRAM_ADDR | LCD_ADDR_LINE3)
	sta LCD_CMD
	rts

lcdLineFour:
	lda #(LCD_CMD_SET_DRAM_ADDR | LCD_ADDR_LINE4)
	sta LCD_CMD
	rts


; Go to next line

nextLine4:
	jsr lcdWait
	; A now contains address
	cmp #LCD_ADDR_LINE4
	bcs lcdLineOne
	cmp #LCD_ADDR_LINE2
	bcs lcdLineThree
	cmp #LCD_ADDR_LINE3
	bcs lcdLineFour
	
	jmp lcdLineTwo

; ---------------------------------

nextLine2:
	jsr lcdWait
	; A now contains address
	cmp #LCD_ADDR_LINE2
	bcs lcdLineOne
	jmp lcdLineTwo

; ---------------------------------

printLine:
	jsr outString
	cpy #CHARS_WIDTH
	beq +
-
	jsr lcdWait
	lda #' '
	sta LCD_DATA
	
	iny
	cpy #CHARS_WIDTH
	bne -
+
	rts	
	

outString:
	ldy #0

-
	jsr lcdWait
	lda (STR_ADDR), y
	beq .end
	adc #1
	sta LCD_DATA
	iny
	jmp -
	
.end:
	rts
	

lcdWait:
	lda LCD_CMD
	bmi lcdWait  ; branch if bit 7 is set
	rts

longDelay:
	jsr delay
	jsr delay
	jsr delay
	jsr delay
	jsr delay
	jsr delay
	jsr delay
	jsr delay
	jsr delay
	; flow through


delay:
	ldx #255
	ldy #255
.loop:
	dex
	bne .loop 
	ldx #255
	dey
	bne .loop
	rts
	


;IMG_DATA_OFFSET = 62  ; Paint
IMG_DATA_OFFSET = 130  ; GIMP

!align 255, 0
!fill 256 - IMG_DATA_OFFSET

livData:
	!bin "liv.bmp"

LIV_IMG = livData + IMG_DATA_OFFSET


!align 255, 0
!fill 256 - IMG_DATA_OFFSET

logoData:
	!bin "logo.bmp"

LOGO_IMG = logoData + IMG_DATA_OFFSET


!align 255, 0
!fill 256 - IMG_DATA_OFFSET

roxData:
	!bin "rox.bmp"

ROX_IMG = roxData + IMG_DATA_OFFSET


!align 255, 0
!fill 256 - IMG_DATA_OFFSET

selfieData:
	!bin "selfie.bmp"

SELFIE_IMG = selfieData + IMG_DATA_OFFSET

*=$FFFC
!word $8000