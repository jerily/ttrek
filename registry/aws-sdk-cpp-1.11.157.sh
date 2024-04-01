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

# aws-sdk-cpp
if true; then
  git -C $BUILD_DIR clone --depth 1 --branch 1.11.157 --recurse-submodules --shallow-submodules https://github.com/aws/aws-sdk-cpp
  cd $BUILD_DIR/aws-sdk-cpp
  mkdir build
  cd build
  cmake .. \
    -DBUILD_SHARED_LIBS=ON \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_ONLY="s3;dynamodb;lambda;sqs;iam;transfer;sts;ssm" \
    -DENABLE_TESTING=OFF \
    -DAUTORUN_UNIT_TESTS=OFF \
    -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR \
    -DCMAKE_PREFIX_PATH=$INSTALL_DIR/ > $BUILD_LOG_DIR/aws-sdk-cpp-configure.log 2>&1
  cmake --build . --config=Release > $BUILD_LOG_DIR/aws-sdk-cpp-build.log 2>&1
  cmake --install . --config=Release > $BUILD_LOG_DIR/aws-sdk-cpp-install.log 2>&1
fi
