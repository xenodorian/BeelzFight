CC      = mips64-linux-gnuabi64-gcc
AS      = mips64-linux-gnuabi64-gcc
LD      = mips64-linux-gnuabi64-gcc
OBJCOPY = mips64-linux-gnuabi64-objcopy

ARCHFLAGS = -march=vr4300 -mabi=32 -mips3 -EB -G0 -fno-pic -mno-abicalls
CFLAGS    = $(ARCHFLAGS) -O2 -fno-builtin -ffreestanding -fomit-frame-pointer \
            -Wall -Wextra -nostdlib -nostartfiles -nodefaultlibs
LDFLAGS   = $(ARCHFLAGS) -nostdlib -nostartfiles -nodefaultlibs -T n64.ld -Wl,--build-id=none

OBJS = src/start.o src/hello.o

all: hello.z64

src/start.o: src/start.S
	$(AS) $(ARCHFLAGS) -c -o $@ $<

src/hello.o: src/hello.c
	$(CC) $(CFLAGS) -c -o $@ $<

hello.elf: $(OBJS) n64.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

hello.bin: hello.elf
	$(OBJCOPY) -O binary -R .bss $< $@

hello.z64: hello.bin ipl3_compat.bin
	python3 tools/make_rom.py ipl3_compat.bin hello.bin $@

clean:
	rm -f $(OBJS) hello.elf hello.bin hello.z64

.PHONY: all clean
