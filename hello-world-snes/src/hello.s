; -----------------------------------------------------------------------
; Hello World (slideshow edition) - minimal SNES (LoROM) program.
;
; Boots to a text "PRESS START" screen (BG1, Mode 0, 2bpp hand-drawn
; font, same technique as the original text-only version of this
; project). Pressing Start switches to Mode 3 (BG1 8bpp) and shows the
; first of three full-screen images; A/B cycle forward/backward through
; them. Button reading uses the SNES's auto-joypad-read hardware
; (enabled once, polled once per frame after waiting for vblank) --
; no SDK, direct register access throughout, same as the rest of this
; project.
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
HVBJOY   = $4212
JOY1L    = $4218
JOY1H    = $4219

; ---------------------------------------------------------------------
; Zero-page (direct page) state
; ---------------------------------------------------------------------
state        = $10   ; 0 = start screen, 1 = slideshow
cur_image    = $11   ; 0..2
prev_start   = $12
prev_a       = $13
prev_b       = $14
cur_start    = $15
cur_a        = $16
cur_b        = $17

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

    stz NMITIMEN        ; NMI/IRQ off, auto-joypad off (for now)
    lda #$8F
    sta INIDISP         ; forced blank while we set everything up

    lda #$80
    sta VMAIN           ; VRAM address increments after the high byte write

    ; -------------------------------------------------------------
    ; "PRESS START" screen: Mode 0, BG1 2bpp font (same technique as
    ; the original text-only version of this project).
    ; -------------------------------------------------------------
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

    seta16
    lda #$0400
    sta VMADDL
    seta8

    ldx #$0000
copy_startmap:
    lda start_tilemap,x
    sta VMDATAL
    lda start_tilemap+1,x
    sta VMDATAH
    inx
    inx
    cpx #TILEMAP_BYTES
    bne copy_startmap
    setxy8

    lda #$00
    sta CGADD
    lda #$00
    sta CGDATA
    lda #$00
    sta CGDATA          ; color 0 = black

    lda #$01
    sta CGADD
    lda #$FF
    sta CGDATA
    lda #$7F
    sta CGDATA          ; color 1 = white

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

    lda #$01
    sta NMITIMEN        ; enable auto-joypad read

    stz state
    stz cur_image
    stz prev_start
    stz prev_a
    stz prev_b

; ---------------------------------------------------------------------
; Main loop
; ---------------------------------------------------------------------
main_loop:
    jsr wait_frame

    lda JOY1L
    and #$10            ; Start button
    sta cur_start

    lda state
    beq start_state
    jmp slideshow_state

start_state:
    lda cur_start
    beq store_prev_start
    lda prev_start
    bne store_prev_start    ; already held, no edge
    lda #$01
    sta state
    stz cur_image
    jsr dispatch_copy
store_prev_start:
    lda cur_start
    sta prev_start
    jmp main_loop

slideshow_state:
    lda JOY1H
    and #$80            ; A button
    sta cur_a
    lda JOY1L
    and #$80            ; B button
    sta cur_b

    lda cur_a
    beq check_b
    lda prev_a
    bne check_b
    jsr next_image
check_b:
    lda cur_b
    beq store_prev_ab
    lda prev_b
    bne store_prev_ab
    jsr prev_image
store_prev_ab:
    lda cur_a
    sta prev_a
    lda cur_b
    sta prev_b
    jmp main_loop

; ---------------------------------------------------------------------
; wait_frame: block until the start of the next vblank, then wait for
; the hardware auto-joypad read to finish so JOY1L/JOY1H are fresh.
; ---------------------------------------------------------------------
wait_frame:
wf_wait_novblank:
    lda HVBJOY
    and #$80
    bne wf_wait_novblank
wf_wait_vblank:
    lda HVBJOY
    and #$80
    beq wf_wait_vblank
wf_wait_autojoy:
    lda HVBJOY
    and #$01
    bne wf_wait_autojoy
    rts

; ---------------------------------------------------------------------
; next_image / prev_image: advance cur_image (mod 3), then copy it in.
; ---------------------------------------------------------------------
next_image:
    lda cur_image
    inc a
    cmp #$03
    bne ni_store
    lda #$00
ni_store:
    sta cur_image
    jmp dispatch_copy

prev_image:
    lda cur_image
    clc
    adc #$02
    cmp #$03
    bcc pi_store
    sec
    sbc #$03
pi_store:
    sta cur_image
    jmp dispatch_copy

; ---------------------------------------------------------------------
; dispatch_copy: forced-blank, ensure Mode 3 is set up, DMA the current
; image's tiles/tilemap/palette in, un-blank. rts.
; ---------------------------------------------------------------------
dispatch_copy:
    lda #$80
    sta INIDISP         ; forced blank while we update VRAM/CGRAM

    lda #$03
    sta BGMODE          ; mode 3: BG1 = 8bpp
    lda #$70
    sta BG1SC           ; map base word $7000, 32x32 single screen
    lda #$00
    sta BG12NBA         ; BG1 tile data base word $0000
    lda #$01
    sta TM              ; enable BG1 on the main screen

    lda cur_image
    beq dc_img0
    cmp #$01
    beq dc_img1
    jsr copy_image2
    bra dc_done
dc_img0:
    jsr copy_image0
    bra dc_done
dc_img1:
    jsr copy_image1
dc_done:
    lda #$0F
    sta INIDISP         ; screen on, full brightness
    rts

; ---------------------------------------------------------------------
; copy_imageN: DMA one image's 512 tiles (32768 + 24576 bytes across
; two ROM banks), 32x32 tilemap, and 256-color palette into VRAM/CGRAM.
; ---------------------------------------------------------------------
.macro copy_image_routine name, bank_a, bank_b, tiles_a_label, tiles_b_label, map_label, pal_label
name:
    seta16
    lda #$0000
    sta VMADDL
    seta8
    lda #bank_a
    pha
    plb
    setxy16
    ldx #$0000
:   lda tiles_a_label,x
    sta VMDATAL
    lda tiles_a_label+1,x
    sta VMDATAH
    inx
    inx
    cpx #$8000
    bne :-
    setxy8

    seta16
    lda #$4000
    sta VMADDL
    seta8
    lda #bank_b
    pha
    plb
    setxy16
    ldx #$0000
:   lda tiles_b_label,x
    sta VMDATAL
    lda tiles_b_label+1,x
    sta VMDATAH
    inx
    inx
    cpx #$6000
    bne :-
    setxy8

    seta16
    lda #$7000
    sta VMADDL
    seta8
    setxy16
    ldx #$0000
:   lda map_label,x
    sta VMDATAL
    lda map_label+1,x
    sta VMDATAH
    inx
    inx
    cpx #$0800
    bne :-
    setxy8

    lda #$00
    sta CGADD
    setxy16
    ldx #$0000
:   lda pal_label,x
    sta CGDATA
    lda pal_label+1,x
    sta CGDATA
    inx
    inx
    cpx #$0200
    bne :-
    setxy8

    lda #$00
    pha
    plb
    rts
.endmacro

copy_image_routine copy_image0, $01, $02, img0_tiles1, img0_tiles2, img0_map, img0_pal
copy_image_routine copy_image1, $03, $04, img1_tiles1, img1_tiles2, img1_map, img1_pal
copy_image_routine copy_image2, $05, $06, img2_tiles1, img2_tiles2, img2_map, img2_pal

nmi_handler:
    rti

irq_handler:
    rti

; -----------------------------------------------------------------------
; Font data: 8x8, 2bpp, plane 1 unused (zero) -> pixels are color 0/1
; only. Covers the letters needed for "PRESS START".
; -----------------------------------------------------------------------
NUM_GLYPHS = 7
GLYPH_SPACE = 0
GLYPH_P = 1
GLYPH_R = 2
GLYPH_E = 3
GLYPH_S = 4
GLYPH_T = 5
GLYPH_A = 6

.macro glyph b0,b1,b2,b3,b4,b5,b6,b7
    .byte b0,$00, b1,$00, b2,$00, b3,$00
    .byte b4,$00, b5,$00, b6,$00, b7,$00
.endmacro

font_tiles:
    ; tile 0: space
    glyph %00000000,%00000000,%00000000,%00000000,%00000000,%00000000,%00000000,%00000000
    ; tile 1: P
    glyph %11111100,%10000010,%10000010,%11111100,%10000000,%10000000,%10000000,%00000000
    ; tile 2: R
    glyph %11111110,%10000001,%10000001,%11111110,%10010000,%10001000,%10000100,%00000000
    ; tile 3: E
    glyph %11111111,%10000000,%10000000,%11111100,%10000000,%10000000,%11111111,%00000000
    ; tile 4: S
    glyph %01111110,%10000000,%10000000,%01111100,%00000010,%00000010,%11111100,%00000000
    ; tile 5: T
    glyph %11111111,%00011000,%00011000,%00011000,%00011000,%00011000,%00011000,%00000000
    ; tile 6: A
    glyph %00111100,%01000010,%10000001,%10000001,%11111111,%10000001,%10000001,%00000000

; -----------------------------------------------------------------------
; "PRESS START" tilemap: 32x32 entries, row 14, centered.
; -----------------------------------------------------------------------
TILEMAP_BYTES = 32*32*2
MSG_ROW = 14
MSG_COL = 10

.macro tile n
    .byte n, $00
.endmacro

start_tilemap:
    .repeat MSG_ROW
        .repeat 32
            tile GLYPH_SPACE
        .endrepeat
    .endrepeat
    .repeat MSG_COL
        tile GLYPH_SPACE
    .endrepeat
    tile GLYPH_P
    tile GLYPH_R
    tile GLYPH_E
    tile GLYPH_S
    tile GLYPH_S
    tile GLYPH_SPACE
    tile GLYPH_S
    tile GLYPH_T
    tile GLYPH_A
    tile GLYPH_R
    tile GLYPH_T
    .repeat 32 - MSG_COL - 11
        tile GLYPH_SPACE
    .endrepeat
    .repeat 32 - MSG_ROW - 1
        .repeat 32
            tile GLYPH_SPACE
        .endrepeat
    .endrepeat

.segment "IMG0A"
img0_tiles1:
    .incbin "assets/img0/tiles_bank1.bin"

.segment "IMG0B"
img0_tiles2:
    .incbin "assets/img0/tiles_bank2.bin"
img0_map:
    .incbin "assets/img0/tilemap.bin"
img0_pal:
    .incbin "assets/img0/palette.bin"

.segment "IMG1A"
img1_tiles1:
    .incbin "assets/img1/tiles_bank1.bin"

.segment "IMG1B"
img1_tiles2:
    .incbin "assets/img1/tiles_bank2.bin"
img1_map:
    .incbin "assets/img1/tilemap.bin"
img1_pal:
    .incbin "assets/img1/palette.bin"

.segment "IMG2A"
img2_tiles1:
    .incbin "assets/img2/tiles_bank1.bin"

.segment "IMG2B"
img2_tiles2:
    .incbin "assets/img2/tiles_bank2.bin"
img2_map:
    .incbin "assets/img2/tilemap.bin"
img2_pal:
    .incbin "assets/img2/palette.bin"

.segment "HEADER"
    .byte "HELLO WORLD SNES     "  ; 21 bytes, space padded
    .byte $20                     ; map mode: LoROM, slow
    .byte $00                     ; cartridge type: ROM only
    .byte $08                     ; ROM size: 256 KB
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
