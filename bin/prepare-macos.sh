#!/usr/bin/env bash
set -euo pipefail

if [[ $(uname -s) != Darwin ]]; then
    echo "macOS release builds require a Mac." >&2
    exit 1
fi
architecture=${1:-$(uname -m)}
if [[ $# -gt 1 || ! $architecture =~ ^(arm64|x86_64)$ ]]; then
    echo "Usage: $0 [arm64|x86_64]" >&2
    exit 1
fi

# Run the entire build, including dependency configure scripts and tests, in
# the target architecture. Intel builds on Apple Silicon require Rosetta.
if [[ $(uname -m) != "$architecture" ]]; then
    exec /usr/bin/arch "-$architecture" /bin/bash --noprofile --norc "$0" "$architecture"
fi
case $architecture in
    arm64) prefix=/opt/homebrew ;;
    x86_64) prefix=/usr/local ;;
esac
export PATH="$prefix/bin:/usr/bin:/bin:/usr/sbin:/sbin"
export CC=/usr/bin/clang CXX=/usr/bin/clang++
export CMAKE="$prefix/bin/cmake" CTEST="$prefix/bin/ctest" PYTHON="$prefix/bin/python3"
for tool in "$CMAKE" "$CTEST" "$PYTHON" "$prefix/bin/pkg-config"; do
    if [[ ! -x $tool ]]; then
        echo "Install this architecture's tools with: $prefix/bin/brew install cmake pkg-config python" >&2
        exit 1
    fi
done
"$PYTHON" -c 'import platform, sys; assert platform.machine() == sys.argv[1]' "$architecture"

cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
target=macos-$architecture
export JOBS=${JOBS:-2} TTREK_MAKE_THREADS=${JOBS:-2} BUILD_TYPE=Release
export TTREK_BUILD_DIR="$PWD/build/$target" TTREK_INSTALL_DIR="$PWD/local-static/$target"
bash ./build-ttrek.sh
lipo -verify_arch "$architecture" "$TTREK_BUILD_DIR/ttrek"
mkdir -p "dist/$target"
cp "$TTREK_BUILD_DIR/ttrek" "dist/$target/ttrek"
