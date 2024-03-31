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

# ksuid-tcl
if true; then
  curl -L -o ksuid-tcl-1.0.3.tar.gz --output-dir $BUILD_DIR https://github.com/jerily/ksuid-tcl/archive/refs/tags/v1.0.3.tar.gz
  tar -xzf $BUILD_DIR/ksuid-tcl-1.0.3.tar.gz -C $BUILD_DIR
  cd $BUILD_DIR/ksuid-tcl-1.0.3
  mkdir build
  cd build
  cmake .. \
    -DTCL_LIBRARY_DIR=$INSTALL_DIR/lib \
    -DTCL_INCLUDE_DIR=$INSTALL_DIR/include \
    -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR \
    -DCMAKE_PREFIX_PATH=$INSTALL_DIR/ > $BUILD_LOG_DIR/ksuid-tcl-configure.log 2>&1
  make install > $BUILD_LOG_DIR/ksuid-tcl-install.log 2>&1
fi