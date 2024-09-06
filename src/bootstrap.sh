#!/bin/bash

set -eo pipefail # exit on error

#ROOT_BUILD_DIR=%s
#INSTALL_DIR=%s
#PROJECT_HOME=%s

#PROJECT_HOME="$(cd "$(dirname "$0")" && pwd)"

PROJECT_HOME="$(pwd)"

ROOT_BUILD_DIR="$PROJECT_HOME/build/build"
INSTALL_DIR="$PROJECT_HOME/build/install"

export PROJECT_HOME

DOWNLOAD_DIR="$ROOT_BUILD_DIR/download"
PATCH_DIR="$ROOT_BUILD_DIR/source"

mkdir -p "$DOWNLOAD_DIR"
mkdir -p "$PATCH_DIR"
mkdir -p "$INSTALL_DIR"

if [ -n "$TTREK_MAKE_THREADS" ]; then
    DEFAULT_THREADS="$TTREK_MAKE_THREADS"
else
    DEFAULT_THREADS="$(nproc 2>/dev/null)" \
        || DEFAULT_THREADS="$(sysctl -n hw.ncpu 2>/dev/null)" \
        || DEFAULT_THREADS="4"
fi

LD_LIBRARY_PATH="$INSTALL_DIR/lib"
PKG_CONFIG_PATH="$INSTALL_DIR/lib/pkgconfig"
export LD_LIBRARY_PATH
export PKG_CONFIG_PATH

PATH="$INSTALL_DIR/bin:$PATH"
export PATH
