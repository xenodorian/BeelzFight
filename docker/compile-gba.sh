#!/usr/bin/env bash
#
# docker/compile-gba.sh -- compile a devkitARM/GBA project inside the
# beelzfight-gbadev container.
#
# Mounts a project directory into the container and runs `make` against its
# Makefile (a normal devkitARM GBA Makefile -- one that includes
# $DEVKITARM/gba_rules, same shape as any example under
# /opt/devkitpro/examples/gba in the image). Build output (the .gba/.elf
# left in the project directory after `make`) is copied out to ./build/
# next to this script for easy access from the host.
#
# Usage:
#   docker/compile-gba.sh path/to/project          # compile a project dir
#   docker/compile-gba.sh path/to/project clean all # custom make target(s)
#
# Environment overrides:
#   IMAGE_TAG=beelzfight-gbadev   # image to run
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGE_TAG="${IMAGE_TAG:-beelzfight-gbadev}"

if [ "$#" -lt 1 ]; then
    echo "Usage: $0 path/to/project [make targets...]" >&2
    exit 1
fi

PROJECT_DIR="$1"
shift
MAKE_TARGETS=("$@")

PROJECT_DIR="$(cd "${PROJECT_DIR}" && pwd)"
OUT_DIR="${SCRIPT_DIR}/build"
mkdir -p "${OUT_DIR}"

echo "==> Compiling ${PROJECT_DIR} with ${IMAGE_TAG} (make ${MAKE_TARGETS[*]:-})"

MAKE_CMD="make"
if [ "${#MAKE_TARGETS[@]}" -gt 0 ]; then
    printf -v quoted '%q ' "${MAKE_TARGETS[@]}"
    MAKE_CMD="make ${quoted}"
fi

docker run --rm \
    -v "${PROJECT_DIR}:/project" \
    -w /project \
    "${IMAGE_TAG}" \
    bash -lc "${MAKE_CMD}"

echo "==> Collecting build output into ${OUT_DIR}"
find "${PROJECT_DIR}" -maxdepth 2 \( -name '*.gba' -o -name '*.elf' \) -newer "${SCRIPT_DIR}/Dockerfile.gbadev" -print -exec cp -v {} "${OUT_DIR}/" \; 2>/dev/null || true

echo "==> Done. Build artifacts (if any matched) are in ${OUT_DIR}"
