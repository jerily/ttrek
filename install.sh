#!/bin/bash

set -eo pipefail # exit on error

export SCRIPT_DIR=$(dirname $(readlink -f $0))
INSTALL_DIR=$SCRIPT_DIR/local
echo "Installing to $INSTALL_DIR"

BUILD_DIR=$SCRIPT_DIR/build
mkdir -p $BUILD_DIR

BUILD_LOG_DIR=$BUILD_DIR/logs
mkdir -p $BUILD_LOG_DIR
export LD_LIBRARY_PATH=$INSTALL_DIR/lib:$INSTALL_DIR/lib64
export PKG_CONFIG_PATH=$INSTALL_DIR/lib/pkgconfig

bash $SCRIPT_DIR/registry/zlib-1.3.1.sh $SCRIPT_DIR
bash $SCRIPT_DIR/registry/openssl-3.2.1.sh $SCRIPT_DIR
bash $SCRIPT_DIR/registry/curl-8.7.1.sh $SCRIPT_DIR
bash $SCRIPT_DIR/registry/tcl-9.0b1.sh $SCRIPT_DIR
bash $SCRIPT_DIR/registry/twebserver-latest.sh $SCRIPT_DIR
bash $SCRIPT_DIR/registry/tdom-0.9.3.sh $SCRIPT_DIR
bash $SCRIPT_DIR/registry/thtml-latest.sh $SCRIPT_DIR
bash $SCRIPT_DIR/registry/abseil-cpp-20230125.0.sh $SCRIPT_DIR
bash $SCRIPT_DIR/registry/protobuf-21.9.sh $SCRIPT_DIR
bash $SCRIPT_DIR/registry/tink-cc-2.1.1.sh $SCRIPT_DIR
bash $SCRIPT_DIR/registry/tink-tcl-2.1.1.sh $SCRIPT_DIR
bash $SCRIPT_DIR/registry/aws-sdk-cpp-1.11.157.sh $SCRIPT_DIR
bash $SCRIPT_DIR/registry/aws-sdk-tcl-latest.sh $SCRIPT_DIR
bash $SCRIPT_DIR/registry/tsession-latest.sh $SCRIPT_DIR
bash $SCRIPT_DIR/registry/ksuid-tcl-1.0.3.sh $SCRIPT_DIR