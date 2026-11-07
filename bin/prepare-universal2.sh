#!/usr/bin/env bash
set -euo pipefail

if [[ $(uname -s) != Darwin ]]; then
    echo "Combining macOS release binaries requires a Mac with lipo." >&2
    exit 1
fi
if [[ $# != 0 ]]; then
    echo "Usage: $0" >&2
    exit 1
fi

cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
lipo dist/macos-arm64/ttrek -verify_arch arm64
lipo dist/macos-x86_64/ttrek -verify_arch x86_64
mkdir -p dist/macos-universal2
lipo -create dist/macos-arm64/ttrek dist/macos-x86_64/ttrek \
    -output dist/macos-universal2/ttrek
lipo dist/macos-universal2/ttrek -verify_arch arm64 x86_64
chmod +x dist/macos-universal2/ttrek
