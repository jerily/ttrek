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

# tink-cc
if true; then
  curl -L -o tink-cc-2.1.1.tar.gz --output-dir $BUILD_DIR https://github.com/tink-crypto/tink-cc/archive/refs/tags/v2.1.1.tar.gz
  tar -xzf $BUILD_DIR/tink-cc-2.1.1.tar.gz -C $BUILD_DIR
  cd $BUILD_DIR/tink-cc-2.1.1
  patch -p1 < $SCRIPT_DIR/registry/tink-cc-2.1.1.diff > $BUILD_LOG_DIR/tink-cc-patch.log 2>&1
  mkdir build
  cd build
  cmake .. \
    -DTINK_BUILD_SHARED_LIB=ON \
    -DTINK_USE_INSTALLED_ABSEIL=ON \
    -DTINK_USE_SYSTEM_OPENSSL=ON \
    -DTINK_USE_INSTALLED_PROTOBUF=ON \
    -DTINK_USE_INSTALLED_RAPIDJSON=OFF \
    -DTINK_BUILD_TESTS=OFF \
    -DCMAKE_SKIP_RPATH=ON \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR \
    -DCMAKE_PREFIX_PATH=$INSTALL_DIR/ > $BUILD_LOG_DIR/tink-cc-configure.log 2>&1
  make install > $BUILD_LOG_DIR/tink-cc-install.log 2>&1
fi
