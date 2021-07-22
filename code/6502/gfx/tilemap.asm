; 6502 - Tilemap
;
; Copyright (c) 2021 Troy Schrapel
;
; This code is licensed under the MIT license
;
; https://github.com/visrealm/hbc-56
;
;
; Tilemap structure
; ---------------------
; BufferAddressH
; Size
; InvertAddressH
; DirtyAddressH


; -------------------------
; Contants
; -------------------------
TILEMAP_SIZE_X_16   = b00000000
TILEMAP_SIZE_X_32   = b00000001
TILEMAP_SIZE_X_64   = b00000010
TILEMAP_SIZE_Y_8    = b00000000
TILEMAP_SIZE_Y_16   = b00000100
TILEMAP_SIZE_Y_32   = b00001000


; Tilemapview structure
; ---------------------
; TilemapAddressH
; ScrollX
; ScrollY
; TileScrollXY




; -------------------------
; Zero page
; -------------------------
TILEMAP_ADDR = R5

; -----------------------------------------------------------------------------
; tilemapInit: Initialise a tilemap
; -----------------------------------------------------------------------------
; Inputs:
;  TILEMAP_ADDR: Address of tilemap structure
; -----------------------------------------------------------------------------
tilemapInit:
	ldy #0
	sty R4L
	sty R4H
	lda (TILEMAP_ADDR), y  ; buffer address L
	sta R0L
	iny
	lda (TILEMAP_ADDR), y  ; buffer address H
	sta R0H
	iny
	lda #128
	sta R4L                ; size in bytes
	lda (TILEMAP_ADDR), y  ; size flags
	beq +
	asl R4L
	inc R4H
	bit TILEMAP_SIZE_X_64


+



	
	
	
; -----------------------------------------------------------------------------
; bitmapFill: Fill the bitmap with value in A
; -----------------------------------------------------------------------------
; Inputs:
;  BITMAP_ADDR_H: Contains page-aligned address of 1-bit 128x64 bitmap
;  A: The value to fill
; -----------------------------------------------------------------------------
bitmapFill:
	sta BITMAP_TMP1
	lda BITMAP_ADDR_H
	sta PIX_ADDR_H
	ldx #0
	stx PIX_ADDR_L

	lda BITMAP_TMP1	
	ldy #0
	ldx #4
-
	sta (PIX_ADDR), y
	iny
	bne -
	inc PIX_ADDR_H
	dex
	bne -
	
	rts
	
	
; -----------------------------------------------------------------------------
; bitmapXor: XOR (invert) the entire bitmap
; -----------------------------------------------------------------------------
; Inputs:
;  BITMAP_ADDR_H: Contains page-aligned address of 1-bit 128x64 bitmap
; -----------------------------------------------------------------------------
bitmapXor:
	lda BITMAP_ADDR_H
	sta PIX_ADDR_H
	ldx #0
	stx PIX_ADDR_L

	ldy #0
	ldx #4
-
	lda #$ff
	eor (PIX_ADDR), y
	sta (PIX_ADDR), y
	
	iny
	bne -
	inc PIX_ADDR_H
	dex
	bne -
	
	rts
	
; -----------------------------------------------------------------------------
; _bitmapOffset: Set up the offset to the buffer based on X/Y (Internal use)
; -----------------------------------------------------------------------------
; Inputs:
;  BITMAP_ADDR_H: Contains page-aligned address of 1-bit 128x64 bitmap
;  BITMAP_X: X position (0 to 127)
;  BITMAP_Y: Y position (0 to 63)
; Outputs:
;  PIX_ADDR: Set to byte at column 0 of row BITMAP_Y
;  Y: 		 Y offset of byte within row (0 to 63)
;  X: 		 Bit offset within the byte
; -----------------------------------------------------------------------------
_bitmapOffset:

	lda BITMAP_ADDR_H
	sta PIX_ADDR_H
	ldx #0
	stx PIX_ADDR_L
	
	lda BITMAP_Y
	lsr
	lsr
	lsr
	lsr
	clc
	adc PIX_ADDR_H
	sta PIX_ADDR_H
	
	lda BITMAP_Y
	and #$0f
	asl
	asl
	asl
	asl
	sta PIX_ADDR_L
	
	lda BITMAP_X
	lsr
	lsr
	lsr
	tay	  ; Y contains start byte offset in row
	
	lda BITMAP_X
	and #$07
	tax   ; X contains bit offset within byte (0 - 7)	
	rts
	
; -----------------------------------------------------------------------------
; bitmapSetPixel: Set a pixel
; -----------------------------------------------------------------------------
; Inputs:
;  BITMAP_ADDR_H: Contains page-aligned address of 1-bit 128x64 bitmap
;  BITMAP_X: X position (0 to 127)
;  BITMAP_Y: Y position (0 to 63)
; -----------------------------------------------------------------------------
bitmapSetPixel:

	jsr _bitmapOffset
	
	lda #$80
	
; shift the bits to the right for the pixel offset
-
	cpx #0
	beq +
	dex
	lsr    
	bcc -  ; carry is always clear
+
	
	ora (PIX_ADDR), y
	sta (PIX_ADDR), y
	
	rts	
	
; -----------------------------------------------------------------------------
; bitmapClearPixel: Clear a pixel
; -----------------------------------------------------------------------------
; Inputs:
;  BITMAP_ADDR_H: Contains page-aligned address of 1-bit 128x64 bitmap
;  BITMAP_X: X position (0 to 127)
;  BITMAP_Y: Y position (0 to 63)
; -----------------------------------------------------------------------------
bitmapClearPixel:

	jsr _bitmapOffset
	
	lda #$80
	
; shift the bits to the right for the pixel offset
-
	cpx #0
	beq +
	dex
	lsr    
	bcc -  ; carry is always clear
+
	eor #$ff
	and (PIX_ADDR), y
	sta (PIX_ADDR), y
	
	rts
	
	
; -----------------------------------------------------------------------------
; bitmapXorPixel: XOR a pixel
; -----------------------------------------------------------------------------
; Inputs:
;  BITMAP_ADDR_H: Contains page-aligned address of 1-bit 128x64 bitmap
;  BITMAP_X: X position (0 to 127)
;  BITMAP_Y: Y position (0 to 63)
; -----------------------------------------------------------------------------
bitmapXorPixel:

	jsr _bitmapOffset
	
	lda #$80
	
; shift the bits to the right for the pixel offset
-
	cpx #0
	beq +
	dex
	lsr    
	bcc -  ; carry is always clear
+
	eor (PIX_ADDR), y
	sta (PIX_ADDR), y
	
	rts
	
; -----------------------------------------------------------------------------
; bitmapLineH: Output a horizontal line
; -----------------------------------------------------------------------------
; Inputs:
;  BITMAP_ADDR_H: Contains page-aligned address of 1-bit 128x64 bitmap
;  BITMAP_X1: Start X position (0 to 127)
;  BITMAP_X2: End X position (0 to 127)
;  BITMAP_Y:  Y position (0 to 63)
; -----------------------------------------------------------------------------
bitmapLineH:

	END_OFFSET   = BITMAP_TMP3
	START_BYTE   = BITMAP_TMP1
	END_BYTE     = BITMAP_TMP2
	TMP_STYLE    = BITMAP_TMP5

	lda BITMAP_X2
	lsr
	lsr
	lsr
	sta END_OFFSET  ; END_OFFSET contains end byte offset within the row

	jsr _bitmapOffset

	lda BITMAP_LINE_STYLE
	sta TMP_STYLE
	
	lda #$ff
	
; shift the bits to the right for the pixel offset
-
	cpx #0
	beq ++
	lsr TMP_STYLE
	bcc +
	pha
	lda #$80
	ora TMP_STYLE
	sta TMP_STYLE
	pla	
+
	dex
	lsr
	bcs -  ; carry is always set
++
	sta START_BYTE

	lda BITMAP_X2
	and #$07
	
	tax   ; X contains bit offset within byte (0 - 7)	
	
	lda #$ff
	
; shift the bits to the left for the pixel offset
-
	cpx #7
	beq +
	inx
	asl    
	bcs -  ; carry is always set
+
	sta END_BYTE
	
	lda START_BYTE
	cpy END_OFFSET
	bne ++
	and END_BYTE  ; combine if within the same byte
	
	pha
	eor #$ff
	and (PIX_ADDR), y
	sta BITMAP_TMP4
	pla
	and TMP_STYLE
	ora BITMAP_TMP4
	sta (PIX_ADDR), y
	
	rts
++
	pha
	eor #$ff
	and (PIX_ADDR), y
	sta BITMAP_TMP4
	pla
	and TMP_STYLE
	ora BITMAP_TMP4
	sta (PIX_ADDR), y
-
	lda #$ff
	iny
	cpy END_OFFSET
	bne +
	and END_BYTE  ; combine if within the same byte
+
	pha
	eor #$ff
	and (PIX_ADDR), y
	sta BITMAP_TMP4
	pla
	and TMP_STYLE
	ora BITMAP_TMP4
	sta (PIX_ADDR), y

	cpy END_OFFSET
	bne -	
	
	rts
	
	
; -----------------------------------------------------------------------------
; bitmapLineV: Output a horizontal line
; -----------------------------------------------------------------------------
; Inputs:
;  BITMAP_ADDR_H: Contains page-aligned address of 1-bit 128x64 bitmap
;  BITMAP_Y1: Start Y position (0 to 63)
;  BITMAP_Y2: End Y position (0 to 63)
;  BITMAP_X:  Y position (0 to 127)
; -----------------------------------------------------------------------------
bitmapLineV:

	COL_BYTE     = BITMAP_TMP1
	STYLE_BYTE   = BITMAP_TMP2

	jsr _bitmapOffset
	
	lda BITMAP_LINE_STYLE
	sta STYLE_BYTE
	
	lda #$80
	
; shift the bits to the right for the pixel offset
-
	cpx #0
	beq +
	dex
	lsr    
	bcc -  ; carry is always clear
+
	sta COL_BYTE
	
	ldx BITMAP_Y1
-
	lda #$80
	bit STYLE_BYTE
	bne +
	; draw a 0
	lda COL_BYTE
	eor #$ff
	and (PIX_ADDR), y	
	sta (PIX_ADDR), y
	jmp ++
+	; draw a 1
	lda COL_BYTE	
	ora (PIX_ADDR), y	
	sta (PIX_ADDR), y
++
		
	cpx BITMAP_Y2
	beq ++
	asl STYLE_BYTE
	bcc +
	inc STYLE_BYTE
+
	inx
	lda #16
	clc
	adc	PIX_ADDR_L
	bcc +
	inc PIX_ADDR_H
+
	sta PIX_ADDR_L
    clc
	bcc -
++
	
	rts

; -----------------------------------------------------------------------------
; bitmapLine: Output an arbitrary line
; -----------------------------------------------------------------------------
; Inputs:
;  BITMAP_ADDR_H: Contains page-aligned address of 1-bit 128x64 bitmap
;  BITMAP_X1: 
;  BITMAP_Y1: 
;  BITMAP_X2: 
;  BITMAP_Y2: 
; -----------------------------------------------------------------------------
bitmapLine:

	LINE_WIDTH = BITMAP_TMP1
	LINE_HEIGHT = BITMAP_TMP2
	
	; get width
	lda BITMAP_X2
	sec
	sbc BITMAP_X1
	
	bpl +
	lda BITMAP_X1
	pha
	lda BITMAP_X2
	sta BITMAP_X1
	pla
	sta BITMAP_X2
	sec
	sbc BITMAP_X1	
+	
	sta LINE_WIDTH

	; get height
	lda BITMAP_Y2
	sec
	sbc BITMAP_Y1

	bpl +
	lda BITMAP_Y1
	pha
	lda BITMAP_Y2
	sta BITMAP_Y1
	pla
	sta BITMAP_Y2
	sec
	sbc BITMAP_Y1	
+	
	sta LINE_HEIGHT
	
	cmp LINE_WIDTH
	bcs .goTall
	jmp _bitmapLineWide
.goTall
	jmp _bitmapLineTall
	
	; rts in above subroutines
	
; ----------------------------------------------------------------------------

_bitmapLineWide:  ; a line that is wider than it is tall
	
	D = R0
	
	Y = BITMAP_TMP3
	
	lda LINE_HEIGHT
	asl
	sec
	sbc LINE_WIDTH
	sta D
	
	lda BITMAP_X
	pha
	
	lda BITMAP_Y1
	sta Y
	
-
	jsr bitmapSetPixel
	lda D
	bpl +
	lda LINE_HEIGHT
	asl
	jmp ++
+
    inc BITMAP_Y1
	lda LINE_WIDTH
	sec
	sbc LINE_HEIGHT
	asl
	eor #$ff
	clc
	adc #1
++
	clc
	adc D
	sta D
	inc BITMAP_X
	lda BITMAP_X2
	cmp BITMAP_X
	bcs -
	
	lda Y
	sta BITMAP_Y1
	
	pla
	sta BITMAP_X
	
	rts
	
_bitmapLineTall:  ; a line that is taller than it is wide
	
	D = R0
	
	X = BITMAP_TMP3
	
	lda LINE_WIDTH
	asl
	sec
	sbc LINE_HEIGHT
	sta D
	
	lda BITMAP_Y
	pha
	
	lda BITMAP_X1
	sta X
	
-
	jsr bitmapSetPixel
	lda D
	bpl +
	lda LINE_WIDTH
	asl
	jmp ++
+
    inc BITMAP_X1
	lda LINE_HEIGHT
	sec
	sbc LINE_WIDTH
	asl
	eor #$ff
	clc
	adc #1
++
	clc
	adc D
	sta D
	inc BITMAP_Y
	lda BITMAP_Y2
	cmp BITMAP_Y
	bcs -

	lda X
	sta BITMAP_X1
	
	pla
	sta BITMAP_Y
	
	rts
	
; -----------------------------------------------------------------------------
; bitmapRect: Output a rectangle outline
; -----------------------------------------------------------------------------
; Inputs:
;  BITMAP_ADDR_H: Contains page-aligned address of 1-bit 128x64 bitmap
;  BITMAP_X1: 
;  BITMAP_Y1: 
;  BITMAP_X2: 
;  BITMAP_Y2: 
; -----------------------------------------------------------------------------
bitmapRect:
	jsr bitmapLineH
	jsr bitmapLineV
	
	lda BITMAP_X1
	pha
	lda BITMAP_X2
	sta BITMAP_X1

	jsr bitmapLineV
	
	pla
	sta BITMAP_X1

	lda BITMAP_Y1
	pha
	lda BITMAP_Y2
	sta BITMAP_Y1
	
	jsr bitmapLineH

	pla
	sta BITMAP_Y1
	
	rts
	
; -----------------------------------------------------------------------------
; bitmapFilledRect: Output a filled rectangle
; -----------------------------------------------------------------------------
; Inputs:
;  BITMAP_ADDR_H: Contains page-aligned address of 1-bit 128x64 bitmap
;  BITMAP_X1: 
;  BITMAP_Y1: 
;  BITMAP_X2: 
;  BITMAP_Y2: 
; -----------------------------------------------------------------------------
bitmapFilledRect:
	lda BITMAP_Y1
	pha
	lda BITMAP_LINE_STYLE
	pha
	
-
	jsr bitmapLineH
	inc BITMAP_Y1

	pla
	sta BITMAP_LINE_STYLE
	pha
	
	lda BITMAP_Y2
	cmp BITMAP_Y1
	beq +

	jsr bitmapLineH
	inc BITMAP_Y1
	
	lda BITMAP_LINE_STYLE_ODD
	sta BITMAP_LINE_STYLE
	
	lda BITMAP_Y2
	cmp BITMAP_Y1
	bne -
+	

	pla
	sta BITMAP_LINE_STYLE
	pla
	sta BITMAP_Y1
	
	rts