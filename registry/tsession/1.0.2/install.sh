#!/bin/bash

set -eo pipefail # exit on error

export SCRIPT_DIR=$1
INSTALL_DIR=$SCRIPT_DIR/local
echo "Installing to $INSTALL_DIR"

BUILD_DIR=$SCRIPT_DIR/build
mkdir -p $BUILD_DIR

BUILD_LOG_DIR=$BUILD_DIR/logs
mkdir -p $BUILD_LOG_DIR
export LD_LIBRARY_PATH=$INSTALL_DIR/lib:$INSTALL_DIR/lib64
export PKG_CONFIG_PATH=$INSTALL_DIR/lib/pkgconfig

# tsession
if true; then
  curl -L -o tsession-1.0.2.tar.gz --output-dir $BUILD_DIR https://github.com/jerily/tsession/archive/refs/tags/v1.0.2.tar.gz
  tar -xzvf $BUILD_DIR/tsession-1.0.2.tar.gz -C $BUILD_DIR
  cd $BUILD_DIR/tsession-1.0.2
  make install PREFIX=$INSTALL_DIR > $BUILD_LOG_DIR/tsession-install.log 2>&1
fi
