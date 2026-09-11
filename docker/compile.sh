#!/usr/bin/env bash
#
# docker/compile.sh -- compile a KOS-based project inside the beelzfight-romdev
# container.
#
# Mounts a project directory into the container, sources the KOS environment,
# and runs `make` against the project's Makefile (a normal KOS project
# Makefile, e.g. one built on top of $KOS_BASE/Makefile.rules -- see any
# example under a cloned KallistiOS checkout's examples/dreamcast/ for the
# shape of one). Build output (anything already present in the project's
# build/ dir after `make`, plus any top-level *.elf it produced) is copied
# out to ./build/ next to this script for easy access from the host.
#
# Usage:
#   docker/compile.sh                     # compile ./  (repo root) with `make`
#   docker/compile.sh path/to/project      # compile a different project dir
#   docker/compile.sh path/to/project clean all   # custom make target(s)
#
# Environment overrides:
#   IMAGE_TAG=beelzfight-romdev   # image to run
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
IMAGE_TAG="${IMAGE_TAG:-beelzfight-romdev}"

PROJECT_DIR="${1:-${REPO_ROOT}}"
shift || true
MAKE_TARGETS=("$@")

PROJECT_DIR="$(cd "${PROJECT_DIR}" && pwd)"
OUT_DIR="${SCRIPT_DIR}/build"
mkdir -p "${OUT_DIR}"

echo "==> Compiling ${PROJECT_DIR} with ${IMAGE_TAG} (make ${MAKE_TARGETS[*]:-})"

# Build the `make` invocation to run inside the container, after sourcing
# the KOS environment (sets KOS_BASE, the cross-compiler PATH, KOS_CFLAGS,
# etc -- see /opt/toolchains/dc/kos/environ.sh in the image).
MAKE_CMD="make"
if [ "${#MAKE_TARGETS[@]}" -gt 0 ]; then
    printf -v quoted '%q ' "${MAKE_TARGETS[@]}"
    MAKE_CMD="make ${quoted}"
fi

docker run --rm \
    -v "${PROJECT_DIR}:/src" \
    -w /src \
    "${IMAGE_TAG}" \
    bash -lc "source \${KOS_BASE}/environ.sh && ${MAKE_CMD}"

echo "==> Collecting build output into ${OUT_DIR}"
find "${PROJECT_DIR}" -maxdepth 3 \( -name '*.elf' -o -name '*.cdi' -o -name '*.gdi' -o -name '*.iso' -o -name '*.bin' \) -newer "${SCRIPT_DIR}/Dockerfile.romdev" -print -exec cp -v {} "${OUT_DIR}/" \; 2>/dev/null || true

echo "==> Done. Build artifacts (if any matched) are in ${OUT_DIR}"
