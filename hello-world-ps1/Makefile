CC      = mipsel-linux-gnu-gcc
AS      = mipsel-linux-gnu-gcc
LD      = mipsel-linux-gnu-gcc
OBJCOPY = mipsel-linux-gnu-objcopy

ARCHFLAGS = -march=r3000 -mabi=32 -mfp32 -EL -G0 -fno-pic -mno-abicalls -mno-shared
CFLAGS    = $(ARCHFLAGS) -O2 -fno-builtin -ffreestanding -fomit-frame-pointer \
            -Wall -Wextra -nostdlib -nostartfiles -nodefaultlibs
LDFLAGS   = $(ARCHFLAGS) -nostdlib -nostartfiles -nodefaultlibs -T ps1.ld -Wl,--build-id=none

OBJS = src/start.o src/hello.o

all: hello.exe

src/start.o: src/start.S
	$(AS) $(ARCHFLAGS) -c -o $@ $<

src/hello.o: src/hello.c
	$(CC) $(CFLAGS) -c -o $@ $<

hello.elf: $(OBJS) ps1.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

hello.exe: hello.elf
	$(OBJCOPY) -O binary -R .bss $< $@

# Requires mkpsxiso (https://github.com/Lameguy64/mkpsxiso) on PATH.
# Builds a standard SYSTEM.CNF-booting disc: hello.exe (this tiny boot
# executable) plus IMAGE.BIN (the raw image data, read off the disc at
# runtime -- see src/hello.c for why it isn't baked into hello.exe).
# IMAGE.BIN's LBA is fixed by iso.xml's file order and hardcoded in
# hello.c as IMAGE_LBA; re-run with -lba if the layout ever changes.
disc: hello.exe
	mkpsxiso -y iso.xml

clean:
	rm -f $(OBJS) hello.elf hello.exe hello.bin hello.cue

.PHONY: all disc clean
