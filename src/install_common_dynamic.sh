#!/bin/bash

set -eo pipefail # exit on error

PACKAGE=%s
VERSION=%s
ROOT_BUILD_DIR=%s
INSTALL_DIR=%s
SOURCE_DIR=%s
PROJECT_HOME=%s

DOWNLOAD_DIR="$ROOT_BUILD_DIR/download"
ARCHIVE_FILE="${PACKAGE}-${VERSION}.archive"
BUILD_DIR="$ROOT_BUILD_DIR/build/${PACKAGE}-${VERSION}"
PATCH_DIR="$ROOT_BUILD_DIR/source"
BUILD_LOG_DIR="$ROOT_BUILD_DIR/logs/${PACKAGE}-${VERSION}"

if [ -z "$SOURCE_DIR" ]; then
    SOURCE_DIR="$ROOT_BUILD_DIR/source/${PACKAGE}-${VERSION}"
    rm -rf "$SOURCE_DIR"
    mkdir -p "$SOURCE_DIR"
fi

mkdir -p "$DOWNLOAD_DIR"
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
rm -rf "$BUILD_LOG_DIR"
mkdir -p "$BUILD_LOG_DIR"

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
