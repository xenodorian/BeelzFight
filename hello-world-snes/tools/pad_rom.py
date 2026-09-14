#!/usr/bin/env python3
"""Pad a ROM file with trailing zero bytes up to a target size (must not
be smaller than the current file). Needed because ld65 only emits the
MEMORY regions that segments actually load into, so a bank with no
segment assigned to it isn't written out.
"""
import sys

def main(path, target_size):
    with open(path, "rb") as f:
        data = bytearray(f.read())

    if len(data) > target_size:
        raise SystemExit(f"ROM is already {len(data)} bytes, larger than target {target_size}")

    data += b"\x00" * (target_size - len(data))

    with open(path, "wb") as f:
        f.write(data)

    print(f"padded to {len(data)} bytes")

if __name__ == "__main__":
    main(sys.argv[1], int(sys.argv[2]))
