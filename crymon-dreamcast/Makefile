CC      = sh-elf-gcc
AS      = sh-elf-gcc
LD      = sh-elf-gcc
OBJCOPY = sh-elf-objcopy

ARCHFLAGS = -m4-single-only -ml
CFLAGS    = $(ARCHFLAGS) -O2 -fno-builtin -ffreestanding -fomit-frame-pointer \
            -Wall -Wextra -nostdlib -nostartfiles -nodefaultlibs
LDFLAGS   = $(ARCHFLAGS) -nostdlib -nostartfiles -nodefaultlibs -T dc.ld -Wl,--build-id=none

OBJS = src/start.o src/main.o

CONTENT_JSON = $(wildcard content/*.json)
CONTENT_INC  = src/content_maps.inc src/content_talk.inc \
               src/content_species.inc src/content_items.inc

all: crymon.elf

$(CONTENT_INC): $(CONTENT_JSON) tools/bake_content.py
	python3 tools/bake_content.py --content content --out src

src/start.o: src/start.S
	$(AS) $(ARCHFLAGS) -c -o $@ $<

src/main.o: src/main.c src/sprites.h $(CONTENT_INC)
	$(CC) $(CFLAGS) -c -o $@ $<

# -lgcc: compiler-support routines (integer divide, struct-copy) GCC
# emits calls to but never inlines, even at -O2 on SH4 -- not part of
# libc/newlib, so this doesn't pull in anything -nodefaultlibs is
# meant to keep out. Needed starting with the battle system's integer
# division and Monster struct copies.
crymon.elf: $(OBJS) dc.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS) -lgcc

# Requires mkdcdisc (https://gitlab.com/simulant/mkdcdisc) on PATH.
cdi: crymon.elf
	mkdcdisc -e crymon.elf -o crymon.cdi -n "CryMon" -a "BeelzFight" \
		-N --allow-overwrite

clean:
	rm -f $(OBJS) crymon.elf crymon.cdi

.PHONY: all cdi clean
