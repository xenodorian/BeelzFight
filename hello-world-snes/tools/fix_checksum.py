#!/usr/bin/env python3
"""Patch the SNES header checksum/complement fields of a LoROM .sfc file.

Assumes the ROM size is an exact power of two (no mirroring needed) and
that the checksum/complement fields are currently zeroed.
"""
import sys

def main(path):
    with open(path, "rb") as f:
        data = bytearray(f.read())

    if len(data) & (len(data) - 1) != 0:
        raise SystemExit(f"ROM size {len(data)} is not a power of two")

    cksum_off = 0x7FDE
    comp_off = 0x7FDC

    data[cksum_off:cksum_off + 2] = b"\x00\x00"
    data[comp_off:comp_off + 2] = b"\x00\x00"

    checksum = sum(data) & 0xFFFF
    complement = checksum ^ 0xFFFF

    data[comp_off:comp_off + 2] = complement.to_bytes(2, "little")
    data[cksum_off:cksum_off + 2] = checksum.to_bytes(2, "little")

    with open(path, "wb") as f:
        f.write(data)

    print(f"checksum={checksum:04X} complement={complement:04X}")

if __name__ == "__main__":
    main(sys.argv[1])
