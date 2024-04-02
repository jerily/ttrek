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


# openssl
if true; then
  curl -L -O --output-dir $BUILD_DIR/ https://www.openssl.org/source/openssl-3.2.1.tar.gz
  tar -xvf $BUILD_DIR/openssl-3.2.1.tar.gz -C $BUILD_DIR
  cd $BUILD_DIR/openssl-3.2.1
  ./Configure --prefix=$INSTALL_DIR > $BUILD_LOG_DIR/openssl-configure.log 2>&1
  make install > $BUILD_LOG_DIR/openssl-install.log 2>&1
fi
