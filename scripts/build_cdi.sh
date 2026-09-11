#!/bin/sh
# Package the built beelzfight.elf + romdisc/ assets into a bootable
# Dreamcast disc image (build/beelzfight.cdi), runnable in an emulator or
# burned for real hardware.
#
# Run this from inside the KOS toolchain container (see docker/), after
# `make` has produced beelzfight.elf at the repo root.
#
# Prefers KOS's all-in-one `mkdcdisc` when available; falls back to the
# classic manual pipeline (kos-objcopy -> scramble -> genisoimage -> cdi4dc)
# otherwise. Either way the ISO root is romdisc/ (which already holds
# textures/*.pvr from scripts/png_to_pvr.py) plus the scrambled 1ST_READ.BIN.

set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

ELF="beelzfight.elf"
OUT_DIR="build"
CDI="$OUT_DIR/beelzfight.cdi"
ISO_DIR="$OUT_DIR/isoroot"

if [ ! -f "$ELF" ]; then
    echo "error: $ELF not found -- run 'make' first" >&2
    exit 1
fi

mkdir -p "$OUT_DIR"

if command -v mkdcdisc >/dev/null 2>&1; then
    echo "==> using mkdcdisc"
    # -D (not -d): "directory-contents", so romdisc/textures/... lands at the
    # ISO root as textures/... -- matching the /cd/textures/... paths baked
    # into include/sprite_data.h -- instead of nested under a romdisc/ dir.
    mkdcdisc -e "$ELF" -D romdisc -o "$CDI" \
        -n "BeelzFight" -a "BeelzFight Team" --allow-overwrite
else
    echo "==> mkdcdisc not found, falling back to scramble+genisoimage+cdi4dc"
    command -v kos-objcopy >/dev/null 2>&1 || { echo "kos-objcopy missing"; exit 1; }
    command -v scramble    >/dev/null 2>&1 || { echo "scramble missing"; exit 1; }
    command -v genisoimage >/dev/null 2>&1 || { echo "genisoimage missing"; exit 1; }
    command -v cdi4dc      >/dev/null 2>&1 || { echo "cdi4dc missing"; exit 1; }

    rm -rf "$ISO_DIR"
    mkdir -p "$ISO_DIR"
    cp -r romdisc/. "$ISO_DIR/"

    kos-objcopy -O binary "$ELF" "$OUT_DIR/beelzfight.bin"
    scramble "$OUT_DIR/beelzfight.bin" "$ISO_DIR/1ST_READ.BIN"

    IP_BIN="$OUT_DIR/IP.BIN"
    if [ -f "romdisc/IP.BIN" ]; then
        IP_BIN="romdisc/IP.BIN"
    elif [ -f "$KOS_BASE/utils/ip_template/ip_template.bin" ]; then
        cp "$KOS_BASE/utils/ip_template/ip_template.bin" "$IP_BIN"
    else
        echo "warning: no IP.BIN found -- disc may not boot on real hardware" >&2
    fi

    ISO="$OUT_DIR/beelzfight.iso"
    if [ -f "$IP_BIN" ]; then
        genisoimage -V BEELZFIGHT -G "$IP_BIN" -joliet -rock -l -o "$ISO" "$ISO_DIR"
    else
        genisoimage -V BEELZFIGHT -joliet -rock -l -o "$ISO" "$ISO_DIR"
    fi
    cdi4dc "$ISO" "$CDI" -d
fi

echo "==> built $CDI"
ls -la "$CDI" 2>/dev/null || true
