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

# aws-sdk-tcl
if true; then
  #curl -L -o aws-sdk-tcl-1.0.2.tar.gz --output-dir $BUILD_DIR https://github.com/jerily/aws-sdk-tcl/archive/refs/tags/v1.0.2.tar.gz
  #tar -xzf $BUILD_DIR/aws-sdk-tcl-1.0.2.tar.gz -C $BUILD_DIR
  git -C $BUILD_DIR clone https://github.com/jerily/aws-sdk-tcl.git
  cd $BUILD_DIR/aws-sdk-tcl
  mkdir build
  cd build
  cmake .. \
    -DTCL_LIBRARY_DIR=$INSTALL_DIR/lib \
    -DTCL_INCLUDE_DIR=$INSTALL_DIR/include \
    -DAWS_SDK_CPP_DIR=$INSTALL_DIR \
    -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR \
    -DCMAKE_PREFIX_PATH=$INSTALL_DIR/ > $BUILD_LOG_DIR/aws-sdk-tcl-configure.log 2>&1
  make install > $BUILD_LOG_DIR/aws-sdk-tcl-install.log 2>&1
fi
