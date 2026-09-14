# Requires a built KallistiOS (KOS) environment sourced first:
#   . /path/to/kallistios/environ.sh
# and mkdcdisc (https://gitlab.com/simulant/mkdcdisc) on PATH for `make cdi`.

TARGET = crymon.elf
OBJS   = src/main.o src/align_compat.o

all: rm-elf $(TARGET)

include $(KOS_BASE)/Makefile.rules

clean: rm-elf
	-rm -f $(OBJS)

rm-elf:
	-rm -f $(TARGET)

# -Wl,--allow-multiple-definition: this stock sh-elf-gcc's newlib was
# built with its own __exit/_errno (lib_a-syscalls.o), which KOS's own
# newlib_exit.o etc. duplicate; dc-chain's patched newlib omits its own
# copies for exactly this reason. Harmless here since KOS's definitions
# (linked first) are the ones we want kept.
$(TARGET): $(OBJS)
	kos-cc -o $(TARGET) $(OBJS) -Wl,--allow-multiple-definition

cdi: $(TARGET)
	mkdcdisc -e $(TARGET) -o crymon.cdi -n "CryMon" -a "BeelzFight" \
		-N --allow-overwrite

.PHONY: all cdi clean rm-elf
