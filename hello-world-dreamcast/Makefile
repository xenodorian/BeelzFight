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

# Requires mkdcdisc (https://gitlab.com/simulant/mkdcdisc) on PATH.
# -N skips GD-ROM padding, for a small (~1.5MB) image; drop it for a
# full-size (~740MB) disc if a burning tool insists on one.
cdi: hello.elf
	mkdcdisc -e hello.elf -o hello.cdi -n "Hello World" -a "BeelzFight" \
		-N --allow-overwrite

clean:
	rm -f $(OBJS) hello.elf hello.cdi

.PHONY: all cdi clean
