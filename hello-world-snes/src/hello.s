; -----------------------------------------------------------------------
; Hello World - minimal SNES (LoROM) program.
;
; Sets up BG1 in Mode 0 with a hand-drawn 8-glyph 2bpp font, writes a
; static tilemap spelling "HELLO WORLD", turns the screen on, and halts.
; No NMI/IRQ use is required since nothing needs to change after setup.
; -----------------------------------------------------------------------

.p816

.macro seta16
    rep #$20
    .A16
.endmacro
.macro seta8
    sep #$20
    .A8
.endmacro
.macro setxy16
    rep #$10
    .I16
.endmacro
.macro setxy8
    sep #$10
    .I8
.endmacro

; ---------------------------------------------------------------------
; PPU / CPU registers used
; ---------------------------------------------------------------------
INIDISP  = $2100
BGMODE   = $2105
BG1SC    = $2107
BG12NBA  = $210B
VMAIN    = $2115
VMADDL   = $2116
VMDATAL  = $2118
VMDATAH  = $2119
CGADD    = $2121
CGDATA   = $2122
TM       = $212C
NMITIMEN = $4200

.segment "CODE"

.A8
.I8

reset_handler:
    sei
    clc
    xce                 ; switch to native (65816) mode

    seta16
    lda #$1FFF
    tcs                 ; native stack pointer

    seta8
    setxy8

    lda #$00
    pha
    plb                 ; data bank register = $00

    stz NMITIMEN        ; NMI/IRQ off, auto-joypad off
    lda #$8F
    sta INIDISP         ; forced blank while we set everything up

    ; -------------------------------------------------------------
    ; Font tiles: 8 glyphs, 2bpp, 16 bytes each -> VRAM word $0000
    ; -------------------------------------------------------------
    lda #$80
    sta VMAIN           ; VRAM address increments after the high byte write

    seta16
    lda #$0000
    sta VMADDL
    seta8

    setxy16
    ldx #$0000
copy_font:
    lda font_tiles,x
    sta VMDATAL
    lda font_tiles+1,x
    sta VMDATAH
    inx
    inx
    cpx #(NUM_GLYPHS*16)
    bne copy_font

    ; -------------------------------------------------------------
    ; Tilemap: 32x32 entries (2 bytes each) -> VRAM word $0400
    ; -------------------------------------------------------------
    seta16
    lda #$0400
    sta VMADDL
    seta8

    ldx #$0000
copy_map:
    lda tilemap,x
    sta VMDATAL
    lda tilemap+1,x
    sta VMDATAH
    inx
    inx
    cpx #TILEMAP_BYTES
    bne copy_map
    setxy8

    ; -------------------------------------------------------------
    ; Palette: color 0 = black (backdrop), color 1 = white (glyph ink)
    ; -------------------------------------------------------------
    lda #$00
    sta CGADD
    lda #$00
    sta CGDATA          ; color 0 low byte
    lda #$00
    sta CGDATA          ; color 0 high byte -> $0000 (black)

    lda #$01
    sta CGADD
    lda #$FF
    sta CGDATA          ; color 1 low byte
    lda #$7F
    sta CGDATA          ; color 1 high byte -> $7FFF (white)

    ; -------------------------------------------------------------
    ; Background setup: Mode 0, BG1 tilemap at $0400, tiles at $0000
    ; -------------------------------------------------------------
    lda #$00
    sta BGMODE          ; mode 0, all backgrounds 2bpp

    lda #$04
    sta BG1SC           ; map base word $0400, 32x32 single screen

    lda #$00
    sta BG12NBA         ; BG1 tile data base word $0000

    lda #$01
    sta TM              ; enable BG1 on the main screen

    lda #$0F
    sta INIDISP         ; screen on, full brightness

forever:
    jmp forever

nmi_handler:
    rti

irq_handler:
    rti

; -----------------------------------------------------------------------
; Font data: 8x8, 2bpp, plane 1 unused (zero) -> pixels are color 0/1 only.
; Each row is (plane0 byte, plane1 byte); plane1 is all zero.
; -----------------------------------------------------------------------
NUM_GLYPHS = 8

.macro glyph b0,b1,b2,b3,b4,b5,b6,b7
    .byte b0,$00, b1,$00, b2,$00, b3,$00
    .byte b4,$00, b5,$00, b6,$00, b7,$00
.endmacro

font_tiles:
    ; tile 0: space
    glyph %00000000,%00000000,%00000000,%00000000,%00000000,%00000000,%00000000,%00000000
    ; tile 1: H
    glyph %10000001,%10000001,%10000001,%11111111,%10000001,%10000001,%10000001,%00000000
    ; tile 2: E
    glyph %11111111,%10000000,%10000000,%11111100,%10000000,%10000000,%11111111,%00000000
    ; tile 3: L
    glyph %10000000,%10000000,%10000000,%10000000,%10000000,%10000000,%11111111,%00000000
    ; tile 4: O
    glyph %01111110,%10000001,%10000001,%10000001,%10000001,%10000001,%01111110,%00000000
    ; tile 5: W
    glyph %10000001,%10000001,%10000001,%10100101,%10100101,%11011011,%10000001,%00000000
    ; tile 6: R
    glyph %11111110,%10000001,%10000001,%11111110,%10010000,%10001000,%10000100,%00000000
    ; tile 7: D
    glyph %11111100,%10000010,%10000001,%10000001,%10000001,%10000010,%11111100,%00000000

; -----------------------------------------------------------------------
; Tilemap: 32x32 entries, 2 bytes each (low byte = tile index, high byte
; = palette/flip/priority, 0 here). "HELLO WORLD" is placed on row 14,
; starting at column 10; every other entry is tile 0 (blank).
; -----------------------------------------------------------------------
TILEMAP_BYTES = 32*32*2

MSG_ROW = 14
MSG_COL = 10

.macro tile n
    .byte n, $00
.endmacro

tilemap:
    .repeat MSG_ROW
        .repeat 32
            tile 0
        .endrepeat
    .endrepeat
    .repeat MSG_COL
        tile 0
    .endrepeat
    tile 1  ; H
    tile 2  ; E
    tile 3  ; L
    tile 3  ; L
    tile 4  ; O
    tile 0  ; space
    tile 5  ; W
    tile 4  ; O
    tile 6  ; R
    tile 3  ; L
    tile 7  ; D
    .repeat 32 - MSG_COL - 11
        tile 0
    .endrepeat
    .repeat 32 - MSG_ROW - 1
        .repeat 32
            tile 0
        .endrepeat
    .endrepeat

.segment "HEADER"
    .byte "HELLO WORLD SNES     "  ; 21 bytes, space padded
    .byte $20                     ; map mode: LoROM, slow
    .byte $00                     ; cartridge type: ROM only
    .byte $05                     ; ROM size: 32 KB
    .byte $00                     ; RAM size: none
    .byte $01                     ; destination code: USA
    .byte $00                     ; fixed / license code
    .byte $00                     ; version
    .word $0000                   ; checksum complement (patched post-link)
    .word $0000                   ; checksum (patched post-link)

.segment "VECTORS"
    ; native mode
    .addr $0000         ; unused
    .addr $0000         ; unused
    .addr irq_handler   ; COP
    .addr irq_handler   ; BRK
    .addr irq_handler   ; ABORT
    .addr nmi_handler   ; NMI
    .addr $0000         ; unused
    .addr irq_handler   ; IRQ
    ; emulation mode
    .addr $0000         ; unused
    .addr $0000         ; unused
    .addr irq_handler   ; COP
    .addr $0000         ; unused
    .addr irq_handler   ; ABORT
    .addr irq_handler   ; NMI
    .addr reset_handler ; RESET
    .addr irq_handler   ; IRQ/BRK
