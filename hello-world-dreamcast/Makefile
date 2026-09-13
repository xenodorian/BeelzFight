CC      = sh-elf-gcc
AS      = sh-elf-gcc
LD      = sh-elf-gcc
OBJCOPY = sh-elf-objcopy

ARCHFLAGS = -m4-single-only -ml
CFLAGS    = $(ARCHFLAGS) -O2 -fno-builtin -ffreestanding -fomit-frame-pointer \
            -Wall -Wextra -nostdlib -nostartfiles -nodefaultlibs
LDFLAGS   = $(ARCHFLAGS) -nostdlib -nostartfiles -nodefaultlibs -T dc.ld -Wl,--build-id=none

OBJS = src/start.o src/hello.o

all: hello.elf

src/start.o: src/start.S
	$(AS) $(ARCHFLAGS) -c -o $@ $<

src/hello.o: src/hello.c
	$(CC) $(CFLAGS) -c -o $@ $<

hello.elf: $(OBJS) dc.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

clean:
	rm -f $(OBJS) hello.elf

.PHONY: all clean
