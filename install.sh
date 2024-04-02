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

bash $SCRIPT_DIR/registry/abseil-cpp/20230802.1/install.sh $SCRIPT_DIR
bash $SCRIPT_DIR/registry/zlib/1.3.1/install.sh $SCRIPT_DIR
bash $SCRIPT_DIR/registry/openssl/3.2.1/install.sh $SCRIPT_DIR
bash $SCRIPT_DIR/registry/curl/8.7.1/install.sh $SCRIPT_DIR
bash $SCRIPT_DIR/registry/tcl/9.0b1/install.sh $SCRIPT_DIR
bash $SCRIPT_DIR/registry/twebserver/1.47.15/install.sh $SCRIPT_DIR
bash $SCRIPT_DIR/registry/tdom/0.9.3/install.sh $SCRIPT_DIR
bash $SCRIPT_DIR/registry/thtml/1.0.0/install.sh $SCRIPT_DIR
bash $SCRIPT_DIR/registry/protobuf/21.9/install.sh $SCRIPT_DIR
bash $SCRIPT_DIR/registry/tink-cc/2.1.1/install.sh $SCRIPT_DIR
bash $SCRIPT_DIR/registry/tink-tcl/2.1.1/install.sh $SCRIPT_DIR
bash $SCRIPT_DIR/registry/aws-sdk-cpp/1.11.157/install.sh $SCRIPT_DIR
bash $SCRIPT_DIR/registry/aws-sdk-tcl/1.0.3/install.sh $SCRIPT_DIR
bash $SCRIPT_DIR/registry/tsession/1.0.2/install.sh $SCRIPT_DIR
bash $SCRIPT_DIR/registry/ksuid-tcl/1.0.3/install.sh $SCRIPT_DIR