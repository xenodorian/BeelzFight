CC      = sh-elf-gcc
AS      = sh-elf-gcc
LD      = sh-elf-gcc
OBJCOPY = sh-elf-objcopy

ARCHFLAGS = -m4-single-only -ml
CFLAGS    = $(ARCHFLAGS) -O2 -fno-builtin -ffreestanding -fomit-frame-pointer \
            -Wall -Wextra -nostdlib -nostartfiles -nodefaultlibs
LDFLAGS   = $(ARCHFLAGS) -nostdlib -nostartfiles -nodefaultlibs -T dc.ld -Wl,--build-id=none

OBJS = src/start.o src/main.o

all: crymon.elf

src/start.o: src/start.S
	$(AS) $(ARCHFLAGS) -c -o $@ $<

src/main.o: src/main.c
	$(CC) $(CFLAGS) -c -o $@ $<

crymon.elf: $(OBJS) dc.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

# Requires mkdcdisc (https://gitlab.com/simulant/mkdcdisc) on PATH.
cdi: crymon.elf
	mkdcdisc -e crymon.elf -o crymon.cdi -n "CryMon" -a "BeelzFight" \
		-N --allow-overwrite

clean:
	rm -f $(OBJS) crymon.elf crymon.cdi

.PHONY: all cdi clean
