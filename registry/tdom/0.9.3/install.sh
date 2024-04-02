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

# tdom
if true; then
  curl -L -o tdom-0.9.3-src.tar.gz --output-dir $BUILD_DIR http://tdom.org/downloads/tdom-0.9.3-src.tar.gz
  tar -xzf $BUILD_DIR/tdom-0.9.3-src.tar.gz -C $BUILD_DIR
  cd $BUILD_DIR/tdom-0.9.3-src/unix
  ../configure --prefix=$INSTALL_DIR --with-tcl=$INSTALL_DIR/lib > $BUILD_LOG_DIR/tdom-configure.log 2>&1
  make install > $BUILD_LOG_DIR/tdom-install.log 2>&1
fi
