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

# tcl
if true; then
  curl -L -O --output-dir $BUILD_DIR http://prdownloads.sourceforge.net/tcl/tcl9.0b1-src.tar.gz
  tar -xvf $BUILD_DIR/tcl9.0b1-src.tar.gz -C $BUILD_DIR
  cd $BUILD_DIR/tcl9.0b1/unix
  ./configure  --prefix=$INSTALL_DIR > $BUILD_LOG_DIR/tcl-configure.log 2>&1
  make install > $BUILD_LOG_DIR/tcl-install.log 2>&1
fi
