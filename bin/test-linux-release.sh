#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 || ! $1 =~ ^(amd64|arm64)$ || ! ${2:-all} =~ ^(all|alma8|al2023|ubuntu)$ ]]; then
    echo "Usage: $0 amd64|arm64 [all|alma8|al2023|ubuntu]" >&2
    exit 1
fi
architecture=$1
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
for libc in glibc musl; do
    if [[ ! -s dist/linux-$architecture-$libc/ttrek ]]; then
        echo "Missing dist/linux-$architecture-$libc/ttrek; run bin/prepare-linux.sh $architecture $libc first." >&2
        exit 1
    fi
done

runtimes=("${2:-all}")
if [[ ${runtimes[0]} == all ]]; then runtimes=(alma8 al2023 ubuntu); fi
for runtime in "${runtimes[@]}"; do
    case $runtime in
        alma8) image=almalinux:8.10@sha256:2d0f2eb2a83cc09dbc2eb92fade4dbe8f709628149e81aa00aec70630406603b ;;
        al2023) image=public.ecr.aws/amazonlinux/amazonlinux:2023@sha256:381352c430f36d8aa6b8b670de06e7e4e5642fe0cb9dbd463bb8d1e79aefc0c7 ;;
        ubuntu) image=ubuntu:24.04@sha256:fe81560990fae86dc4743202b2cb4d3df98d0bcf949173cecef15ce1127ef852 ;;
    esac
    echo "Testing Linux $architecture glibc and musl on $runtime" >&2
    docker buildx build --platform "linux/$architecture" \
        -f docker/Dockerfile.runtime-test --build-arg "BASE_IMAGE=$image" \
        --no-cache-filter verify --output type=cacheonly .
done
