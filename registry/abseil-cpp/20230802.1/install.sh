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

# abseil-cpp
if true; then
  #VERSION=20230125.0
  VERSION=20230802.1
  curl -L -o abseil-cpp-$VERSION.tar.gz --output-dir $BUILD_DIR https://github.com/abseil/abseil-cpp/archive/refs/tags/$VERSION.tar.gz
  tar -xzf $BUILD_DIR/abseil-cpp-$VERSION.tar.gz -C $BUILD_DIR
  cd $BUILD_DIR/abseil-cpp-$VERSION
  mkdir build
  cd build
  cmake .. \
    -DBUILD_SHARED_LIBS=ON \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_STANDARD=14 \
    -DABSL_PROPAGATE_CXX_STD=ON \
    -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR \
    -DCMAKE_PREFIX_PATH=$INSTALL_DIR/ > $BUILD_LOG_DIR/abseil-cpp-configure.log 2>&1
  make install > $BUILD_LOG_DIR/abseil-cpp-install.log 2>&1
fi
