#!/usr/bin/env bash
set -euo pipefail

if [[ $# != 2 || ! $1 =~ ^(amd64|arm64)$ || ! $2 =~ ^(glibc|musl)$ ]]; then
    echo "Usage: $0 amd64|arm64 glibc|musl" >&2
    exit 1
fi

cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
case $2 in
    glibc) base_image=ubuntu:22.04 ;;
    musl) base_image=alpine:3.23 ;;
esac

exec docker buildx build --platform "linux/$1" -f docker/Dockerfile.release \
    --build-arg "BASE_IMAGE=$base_image" --build-arg "JOBS=${JOBS:-2}" \
    --output "type=local,dest=dist/linux-$1-$2" .
