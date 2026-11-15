#!/bin/sh
# Build the DREAM Linux build image and run linux/build.sh in it, with the
# checkout bind-mounted at /src (build output lands in build/linux-<arch>,
# codec libs in .deps/linux-<arch>, tarballs in dist/).
#
#   linux/docker/run.sh [--platform linux/amd64|linux/arm64] [build.sh options...]
#   linux/docker/run.sh --test --package --deb
#   linux/docker/run.sh --shell          interactive shell in the container
set -eu

REPO=$(cd "$(dirname "$0")/../.." && pwd)
PLATFORM=""
if [ "${1:-}" = "--platform" ]; then
    PLATFORM=$2; shift 2
fi
[ -n "$PLATFORM" ] || PLATFORM=linux/$(docker version --format '{{.Server.Arch}}')
tag=dream-linux-build:$(echo "$PLATFORM" | sed 's|.*/||')

docker build --platform "$PLATFORM" -t "$tag" "$REPO/linux/docker"
if [ "${1:-}" = "--shell" ]; then
    exec docker run --rm -it --platform "$PLATFORM" -v "$REPO:/src" "$tag" bash
fi
# a git worktree's .git points outside the mount: pass the revision for the .deb version
rev=$(git -C "$REPO" rev-parse --short HEAD 2>/dev/null || echo unknown)
exec docker run --rm --platform "$PLATFORM" -e DREAM_GIT_REV="$rev" -v "$REPO:/src" "$tag" /src/linux/build.sh "$@"
