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

#curl
if true; then
  curl -L -O --output-dir $BUILD_DIR https://curl.se/download/curl-8.7.1.tar.gz
  tar -xvf $BUILD_DIR/curl-8.7.1.tar.gz -C $BUILD_DIR
  cd $BUILD_DIR/curl-8.7.1
  ./configure --prefix=$INSTALL_DIR --with-openssl=$INSTALL_DIR --with-zlib=$INSTALL_DIR > $BUILD_LOG_DIR/curl-configure.log 2>&1
  make install > $BUILD_LOG_DIR/curl-install.log 2>&1
fi
