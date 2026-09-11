# BeelzFight -- Dreamcast homebrew, built with KallistiOS (KOS).
#
# Requires a KOS environment sourced first (source $KOS_BASE/environ.sh, or
# run inside docker/Dockerfile.romdev -- see docker/README.md).

TARGET = beelzfight.elf
OBJS = src/main.o src/video.o src/texture.o src/input.o src/assets.o \
       src/player.o src/enemy.o src/boss.o src/level.o src/hud.o

KOS_CFLAGS += -Iinclude -Wall -O2

all: rm-elf $(TARGET)

include $(KOS_BASE)/Makefile.rules

clean:
	-rm -f $(TARGET) $(OBJS)

rm-elf:
	-rm -f $(TARGET)

$(TARGET): $(OBJS)
	kos-cc -o $(TARGET) $(OBJS) -lm

run: $(TARGET)
	$(KOS_LOADER) $(TARGET)

.PHONY: all clean rm-elf run assets
assets:
	python3 scripts/gen_sprites.py
	python3 scripts/png_to_pvr.py
