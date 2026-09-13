#!/usr/bin/env python3
"""Combine libdragon's IPL3 (compat mode) bootcode with a flat binary to
produce a bootable N64 ROM.

IPL3 compat mode (see boot/loader_compat.c in libdragon) reads two
big-endian 32-bit words from the ROM header it ships with: the load
address / entry point at offset 0x08, and the number of bytes to load
at offset 0x10. Both already default to sane values in the template
(0x80000400, and 0 meaning "default to 1MB") -- this script only patches
the size field to the flat binary's exact size, so the loader never
reads past the end of this ROM file.
"""
import struct
import sys

def main(ipl3_path, bin_path, out_path):
    with open(ipl3_path, "rb") as f:
        ipl3 = bytearray(f.read())

    if len(ipl3) != 4096:
        raise SystemExit(f"expected a 4096-byte IPL3 compat blob, got {len(ipl3)}")

    with open(bin_path, "rb") as f:
        payload = f.read()

    struct.pack_into(">I", ipl3, 0x10, len(payload))

    with open(out_path, "wb") as f:
        f.write(ipl3)
        f.write(payload)

    print(f"wrote {out_path}: {4096 + len(payload)} bytes "
          f"(4096-byte IPL3 + {len(payload)}-byte payload)")

if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit(f"usage: {sys.argv[0]} ipl3_compat.bin hello.bin hello.z64")
    main(*sys.argv[1:])
