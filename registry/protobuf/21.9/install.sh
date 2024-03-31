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

# protobuf
if true; then
  curl -L -o protobuf-v21.9.zip --output-dir $BUILD_DIR https://github.com/protocolbuffers/protobuf/archive/v21.9.zip
  unzip $BUILD_DIR/protobuf-v21.9.zip -d $BUILD_DIR
  cd $BUILD_DIR/protobuf-21.9
  mkdir build
  cd build
  cmake .. \
  -DBUILD_SHARED_LIBS=ON \
  -DCMAKE_BUILD_TYPE=Release \
  -Dprotobuf_BUILD_TESTS=OFF \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -Dprotobuf_BUILD_SHARED_LIBS=ON -DCMAKE_CXX_FLAGS="-fPIC" \
  -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR \
  -DCMAKE_PREFIX_PATH=$INSTALL_DIR/ > $BUILD_LOG_DIR/protobuf-configure.log 2>&1
  make install > $BUILD_LOG_DIR/protobuf-install.log 2>&1
fi
