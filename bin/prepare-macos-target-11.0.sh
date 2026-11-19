#!/usr/bin/env bash
set -euo pipefail

if [[ $# != 0 || $(uname -s) != Darwin ]]; then
    echo "Usage: $0 (run on a Mac)" >&2
    exit 1
fi

script_dir=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
export MACOSX_DEPLOYMENT_TARGET=11.0

# sysctl identifies Apple Silicon even when this shell is running in Rosetta.
if [[ $(sysctl -n hw.optional.arm64 2>/dev/null || true) == 1 ]]; then
    "$script_dir/prepare-macos.sh" arm64
    # Comment the new two lines to stop producing Intel and universal2 release builds.
    "$script_dir/prepare-macos.sh" x86_64
    "$script_dir/prepare-universal2.sh"
    # Docker Desktop runs ARM64 Linux natively on Apple Silicon.
    "$script_dir/prepare-linux.sh" arm64 glibc
    "$script_dir/prepare-linux.sh" arm64 musl
    "$script_dir/test-linux-release.sh" arm64
else
    # "$script_dir/prepare-macos.sh" x86_64
    echo "macOS release builds require Apple Silicon." >&2
    exit 1
fi
