#!/usr/bin/env bash
#
# docker/build.sh -- build the beelzfight-romdev Docker image.
#
# The image bundles a full KallistiOS (KOS) Dreamcast SDK built from pinned
# source: sh-elf + arm-eabi cross toolchains, libkallisti, kos-ports basics
# (zlib, libpng), the pvrtex/kmgenc PNG->PVR texture converters, and
# mkdcdisc + genisoimage for packaging a bootable disc image.
#
# This is a from-source build (see docker/Dockerfile.romdev) and can take
# 30-90+ minutes the first time, mostly spent building GCC/Binutils/Newlib
# twice (once per target CPU). Subsequent runs reuse Docker's layer cache
# unless the pinned commits or the Dockerfile itself change.
#
# Usage:
#   docker/build.sh                 # build the "beelzfight-romdev:latest" image
#   IMAGE_TAG=myimage docker/build.sh
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGE_TAG="${IMAGE_TAG:-beelzfight-romdev}"

echo "==> Building ${IMAGE_TAG} from ${SCRIPT_DIR}/Dockerfile.romdev"
echo "    (this builds the full KOS SDK from source; expect 30-90+ minutes)"

docker build \
    -t "${IMAGE_TAG}" \
    -f "${SCRIPT_DIR}/Dockerfile.romdev" \
    "${SCRIPT_DIR}" \
    "$@"

echo "==> Built image: ${IMAGE_TAG}"
echo "    Try it out:   docker run --rm -it ${IMAGE_TAG} sh-elf-gcc --version"
echo "    Compile:      docker/compile.sh"
