    .section .rodata
    .global _image_rgb565
    .align 4
_image_rgb565:
    .incbin "src/assets/image_rgb565.bin"
    .global _image_rgb565_end
_image_rgb565_end:
