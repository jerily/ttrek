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
    "$script_dir/prepare-macos.sh" x86_64
    "$script_dir/prepare-universal2.sh"
else
    "$script_dir/prepare-macos.sh" x86_64
fi
