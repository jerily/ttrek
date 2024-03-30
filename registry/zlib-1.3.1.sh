#!/bin/bash

set -eo pipefail # exit on error

export SCRIPT_DIR=$1
INSTALL_DIR=$SCRIPT_DIR/local
echo "Installing to $INSTALL_DIR"

BUILD_DIR=$SCRIPT_DIR/build
mkdir -p $BUILD_DIR

BUILD_LOG_DIR=$BUILD_DIR/logs
mkdir -p $BUILD_LOG_DIR

export PKG_CONFIG_PATH=$INSTALL_DIR/lib/pkgconfig

# zlib
if true; then
  curl -L -o zlib-1.3.1.tar.gz --output-dir $BUILD_DIR https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz
  tar -xvf $BUILD_DIR/zlib-1.3.1.tar.gz -C $BUILD_DIR
  cd $BUILD_DIR/zlib-1.3.1
  ./configure --prefix=$INSTALL_DIR > $BUILD_LOG_DIR/zlib-configure.log 2>&1
  make install > $BUILD_LOG_DIR/zlib-install.log 2>&1
fi
