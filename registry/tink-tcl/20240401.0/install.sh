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

# tink-tcl
if true; then
  curl -L -o tink-tcl-20240401.0.tar.gz --output-dir $BUILD_DIR https://github.com/jerily/tink-tcl/archive/refs/tags/v20240401.0.tar.gz
  tar xzvf tink-tcl-20240401.0.tar.gz -C $BUILD_DIR
  cd $BUILD_DIR/tink-tcl-20240401.0
  mkdir build
  cd build
  cmake .. \
    -DTCL_LIBRARY_DIR=$INSTALL_DIR/lib \
    -DTCL_INCLUDE_DIR=$INSTALL_DIR/include \
    -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR \
    -DCMAKE_PREFIX_PATH=$INSTALL_DIR/ > $BUILD_LOG_DIR/tink-tcl-configure.log 2>&1
  make install > $BUILD_LOG_DIR/tink-tcl-install.log 2>&1
fi
