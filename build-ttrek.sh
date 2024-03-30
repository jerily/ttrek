#!/bin/bash


set -eo pipefail # exit on error
export SCRIPT_DIR=$(dirname $(readlink -f $0))
BUILD_DIR=$SCRIPT_DIR/build

mkdir -p $BUILD_DIR
cd $BUILD_DIR
curl -L -o cjson-1.7.17.tar.gz --output-dir $BUILD_DIR https://github.com/DaveGamble/cJSON/archive/refs/tags/v1.7.17.tar.gz
tar -xzvf cjson-1.7.17.tar.gz
cd cJSON-1.7.17
mkdir build
cd build
cmake .. -DBUILD_SHARED_LIBS=off -DCMAKE_INSTALL_PREFIX=$BUILD_DIR/static-cjson-1.7.17
make
make install

cd $BUILD_DIR
curl -L -O https://sourceforge.net/projects/tcl/files/Tcl/8.6.13/tcl8.6.13-src.tar.gz
tar -xzvf tcl8.6.13-src.tar.gz
cd tcl8.6.13/unix
./configure --prefix=$BUILD_DIR/static-tcl8.6.13 --disable-shared
make
make install
cd $BUILD_DIR
cmake .. \
  -DTCL_LIBRARY=$BUILD_DIR/static-tcl8.6.13/lib/libtcl8.6.a \
  -DTCL_INCLUDE_DIR=$BUILD_DIR/static-tcl8.6.13/include \
  -DCJSON_LIBRARY=$BUILD_DIR/static-cjson-1.7.17/lib/libcjson.a \
  -DCJSON_INCLUDE_DIR=$BUILD_DIR/static-cjson-1.7.17/include
make