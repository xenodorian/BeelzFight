// Minimal GBA toolchain smoke test: boots into text mode and prints a
// banner, to confirm devkitARM + libgba can build and boot a real ROM
// before any BeelzFight game code is ported over.
#include <gba_console.h>
#include <gba_video.h>
#include <gba_interrupt.h>
#include <gba_systemcalls.h>
#include <stdio.h>

int main(void) {
	irqInit();
	irqEnable(IRQ_VBLANK);

	consoleDemoInit();

	iprintf("\n\n   BEELZFIGHT\n");
	iprintf("   GBA toolchain OK\n");

	while (1) {
		VBlankIntrWait();
	}

	return 0;
}
