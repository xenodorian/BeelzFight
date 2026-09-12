#!/usr/bin/env bash
#
# docker/build-gba.sh -- build the beelzfight-gbadev Docker image.
#
# Unlike build.sh (the Dreamcast/KOS image, built from source), this just
# pulls devkitPro's official, prebuilt devkitARM image through
# mirror.gcr.io and tags it -- see docker/Dockerfile.gbadev for why. Should
# take seconds, not tens of minutes.
#
# Usage:
#   docker/build-gba.sh                 # build the "beelzfight-gbadev:latest" image
#   IMAGE_TAG=myimage docker/build-gba.sh
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGE_TAG="${IMAGE_TAG:-beelzfight-gbadev}"

echo "==> Building ${IMAGE_TAG} from ${SCRIPT_DIR}/Dockerfile.gbadev"

docker build \
    -t "${IMAGE_TAG}" \
    -f "${SCRIPT_DIR}/Dockerfile.gbadev" \
    "${SCRIPT_DIR}" \
    "$@"

echo "==> Built image: ${IMAGE_TAG}"
echo "    Try it out:   docker run --rm -it ${IMAGE_TAG} arm-none-eabi-gcc --version"
echo "    Compile:      docker/compile-gba.sh gba/hello"
