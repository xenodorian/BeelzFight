; -----------------------------------------------------------------------
; Hello World (image edition) - minimal SNES (LoROM) program.
;
; Sets up BG1 in Mode 3 (8bpp) with a full-screen 256x224 image
; (256-color quantized, letterboxed to preserve aspect ratio), turns the
; screen on, and halts. No NMI/IRQ use is required since nothing needs
; to change after setup.
;
; The image data (896 unique 8x8 tiles + tilemap + 256-color palette) is
; too big for one 32KB LoROM bank, so it's split across two extra ROM
; banks (TILES1/TILES2) and copied to VRAM by temporarily switching the
; data bank register (DBR) to point at each one in turn. PPU registers
; at $21xx/$42xx are mirrored into every bank in $00-$3F, so "sta VMDATAL"
; etc. keep working correctly regardless of what DBR is set to.
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

    lda #$80
    sta VMAIN           ; VRAM address increments after the high byte write

    ; -------------------------------------------------------------
    ; Tiles, part 1: bank $01, 32768 bytes -> VRAM word $0000
    ; -------------------------------------------------------------
    seta16
    lda #$0000
    sta VMADDL
    seta8

    lda #$01
    pha
    plb                 ; DBR = bank 1 (TILES1 segment)

    setxy16
    ldx #$0000
copy_tiles1:
    lda tiles_bank1,x
    sta VMDATAL
    lda tiles_bank1+1,x
    sta VMDATAH
    inx
    inx
    cpx #$8000          ; 32768 bytes
    bne copy_tiles1

    ; -------------------------------------------------------------
    ; Tiles, part 2: bank $02, 24576 bytes -> VRAM word $4000
    ; -------------------------------------------------------------
    setxy8
    seta16
    lda #$4000
    sta VMADDL
    seta8

    lda #$02
    pha
    plb                 ; DBR = bank 2 (TILES2 segment)

    setxy16
    ldx #$0000
copy_tiles2:
    lda tiles_bank2,x
    sta VMDATAL
    lda tiles_bank2+1,x
    sta VMDATAH
    inx
    inx
    cpx #$6000          ; 24576 bytes
    bne copy_tiles2

    ; -------------------------------------------------------------
    ; Tilemap: 32x32 entries (2 bytes each) -> VRAM word $7000
    ; -------------------------------------------------------------
    setxy8
    seta16
    lda #$7000
    sta VMADDL
    seta8

    setxy16
    ldx #$0000
copy_map:
    lda tilemap_data,x
    sta VMDATAL
    lda tilemap_data+1,x
    sta VMDATAH
    inx
    inx
    cpx #$0800          ; 2048 bytes
    bne copy_map
    setxy8

    ; -------------------------------------------------------------
    ; Palette: 256 BGR555 entries (still DBR = bank 2)
    ; -------------------------------------------------------------
    lda #$00
    sta CGADD
    setxy16
    ldx #$0000
copy_pal:
    lda palette_data,x
    sta CGDATA
    lda palette_data+1,x
    sta CGDATA
    inx
    inx
    cpx #$0200          ; 512 bytes = 256 entries
    bne copy_pal
    setxy8

    lda #$00
    pha
    plb                 ; DBR back to bank 0

    ; -------------------------------------------------------------
    ; Background setup: Mode 3, BG1 8bpp, tiles at word $0000,
    ; tilemap at word $7000 (32x32 map)
    ; -------------------------------------------------------------
    lda #$03
    sta BGMODE          ; mode 3: BG1 = 8bpp

    lda #$70
    sta BG1SC           ; map base word $7000, 32x32 single screen

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

.segment "TILES1"
tiles_bank1:
    .incbin "assets/tiles_bank1.bin"

.segment "TILES2"
tiles_bank2:
    .incbin "assets/tiles_bank2.bin"
tilemap_data:
    .incbin "assets/tilemap.bin"
palette_data:
    .incbin "assets/palette.bin"

.segment "HEADER"
    .byte "HELLO WORLD SNES     "  ; 21 bytes, space padded
    .byte $20                     ; map mode: LoROM, slow
    .byte $00                     ; cartridge type: ROM only
    .byte $07                     ; ROM size: 128 KB
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
