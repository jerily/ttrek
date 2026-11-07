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
lipo -verify_arch arm64 dist/macos-arm64/ttrek
lipo -verify_arch x86_64 dist/macos-x86_64/ttrek
mkdir -p dist/macos-universal2
lipo -create dist/macos-arm64/ttrek dist/macos-x86_64/ttrek \
    -output dist/macos-universal2/ttrek
lipo -verify_arch arm64 x86_64 dist/macos-universal2/ttrek
chmod +x dist/macos-universal2/ttrek
