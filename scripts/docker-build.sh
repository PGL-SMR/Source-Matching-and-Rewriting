#!/usr/bin/env bash
#
# Build SMR inside the development image, with the repo mounted where the
# devcontainer mounts it, so the same build directory works in both.
#
# Usage: scripts/docker-build.sh [TARGET]
#
# Environment:
#   SMR_IMAGE   Docker image (default: smr-dev-upstream, see docker/DockerfileUpstream)
#   BUILD_DIR   Build directory relative to the repo (default: build)
#   BUILD_TYPE  CMake build type used on the first configure (default: RelWithDebInfo)

set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd -P)
SMR_IMAGE=${SMR_IMAGE:-smr-dev-upstream}
BUILD_DIR=${BUILD_DIR:-build}
BUILD_TYPE=${BUILD_TYPE:-RelWithDebInfo}
TARGET=${1:-smr}
MOUNT=/root/smr

if ! docker image inspect "$SMR_IMAGE" >/dev/null 2>&1; then
  echo "Image '$SMR_IMAGE' not found. Build it with:" >&2
  echo "  docker build -t $SMR_IMAGE -f docker/DockerfileUpstream docker" >&2
  exit 1
fi

docker run --rm \
  -v "$ROOT:$MOUNT" \
  -w "$MOUNT" \
  "$SMR_IMAGE" \
  bash -c "
    set -euo pipefail
    if [ ! -f '$BUILD_DIR/build.ninja' ]; then
      cmake -G Ninja -S . -B '$BUILD_DIR' \
        -DCMAKE_BUILD_TYPE='$BUILD_TYPE' \
        -DCMAKE_C_COMPILER=clang-20 \
        -DCMAKE_CXX_COMPILER=clang++-20
    fi
    ninja -C '$BUILD_DIR' '$TARGET'
  "
