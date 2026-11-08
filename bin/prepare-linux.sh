#!/usr/bin/env bash
set -euo pipefail

if [[ $# != 2 || ! $1 =~ ^(amd64|arm64)$ || ! $2 =~ ^(glibc|musl)$ ]]; then
    echo "Usage: $0 amd64|arm64 glibc|musl" >&2
    exit 1
fi

if ! command -v docker >/dev/null 2>&1; then
    echo "Linux release builds require Docker with the Buildx plugin." >&2
    exit 1
fi
if ! docker buildx version >/dev/null 2>&1; then
    echo "Linux release builds require the Docker Buildx plugin." >&2
    echo "On macOS with Homebrew: brew install docker-buildx" >&2
    echo "Follow the installation instructions from brew info docker-buildx to enable the plugin." >&2
    exit 1
fi

cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
case $1-$2 in
    # Pin the compiler/sysroot together; both images use glibc 2.28.
    amd64-glibc) base_image=quay.io/pypa/manylinux_2_28_x86_64@sha256:9555ba6c479fa69a2aa80ad1dc57154a60d562b19df159361d9379a02beb1e36 ;;
    arm64-glibc) base_image=quay.io/pypa/manylinux_2_28_aarch64@sha256:cb37f0d66c8a75abb93cf6085cad1f1427cd7208f0c0fc48359bb1eea9924f21 ;;
    *-musl) base_image=alpine:3.23 ;;
esac

exec docker buildx build --platform "linux/$1" -f docker/Dockerfile.release \
    --build-arg "BASE_IMAGE=$base_image" --build-arg "LIBC=$2" --build-arg "JOBS=${JOBS:-2}" \
    --output "type=local,dest=dist/linux-$1-$2" .
