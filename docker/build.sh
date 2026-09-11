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
# Building behind a TLS-inspecting proxy (corporate network, some CI
# sandboxes): if git/curl/meson inside the build fail with certificate
# errors, point EXTRA_CA_BUNDLE at that proxy's CA certificate (PEM) and
# re-run -- it's passed to the build as an ephemeral BuildKit secret, never
# written into the image or this repo:
#   EXTRA_CA_BUNDLE=/path/to/proxy-ca.pem docker/build.sh
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGE_TAG="${IMAGE_TAG:-beelzfight-romdev}"

SECRET_ARGS=()
if [ -n "${EXTRA_CA_BUNDLE:-}" ]; then
    if [ ! -s "${EXTRA_CA_BUNDLE}" ]; then
        echo "EXTRA_CA_BUNDLE is set but '${EXTRA_CA_BUNDLE}' is missing or empty" >&2
        exit 1
    fi
    echo "==> Trusting extra CA bundle: ${EXTRA_CA_BUNDLE}"
    SECRET_ARGS=(--secret "id=extra_ca,src=${EXTRA_CA_BUNDLE}")
fi

echo "==> Building ${IMAGE_TAG} from ${SCRIPT_DIR}/Dockerfile.romdev"
echo "    (this builds the full KOS SDK from source; expect 30-90+ minutes)"

docker build \
    -t "${IMAGE_TAG}" \
    -f "${SCRIPT_DIR}/Dockerfile.romdev" \
    "${SECRET_ARGS[@]}" \
    "${SCRIPT_DIR}" \
    "$@"

echo "==> Built image: ${IMAGE_TAG}"
echo "    Try it out:   docker run --rm -it ${IMAGE_TAG} sh-elf-gcc --version"
echo "    Compile:      docker/compile.sh"
