#!/bin/bash


set -eo pipefail # exit on error
export SCRIPT_DIR=$(dirname $(readlink -f $0))
BUILD_DIR=$SCRIPT_DIR/build
INSTALL_DIR=$SCRIPT_DIR/local-static

mkdir -p $BUILD_DIR
cd $BUILD_DIR
curl -L -o zlib-1.3.1.tar.gz --output-dir $BUILD_DIR https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz
tar -xvf $BUILD_DIR/zlib-1.3.1.tar.gz -C $BUILD_DIR
cd $BUILD_DIR/zlib-1.3.1
./configure --prefix=$INSTALL_DIR
make install

mkdir -p $BUILD_DIR
cd $BUILD_DIR
curl -L -O --output-dir $BUILD_DIR/ https://www.openssl.org/source/openssl-3.2.1.tar.gz
tar -xvf $BUILD_DIR/openssl-3.2.1.tar.gz -C $BUILD_DIR
cd $BUILD_DIR/openssl-3.2.1
./Configure --prefix=$INSTALL_DIR -no-shared -no-pinshared
make install

mkdir -p $BUILD_DIR
cd $BUILD_DIR
curl -L -o cjson-1.7.17.tar.gz --output-dir $BUILD_DIR https://github.com/DaveGamble/cJSON/archive/refs/tags/v1.7.17.tar.gz
tar -xzvf cjson-1.7.17.tar.gz
cd cJSON-1.7.17
mkdir build
cd build
cmake .. -DBUILD_SHARED_LIBS=off -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR
make
make install

mkdir -p $BUILD_DIR
cd $BUILD_DIR
curl -L -O --output-dir $BUILD_DIR https://curl.se/download/curl-8.7.1.tar.gz
tar -xvf $BUILD_DIR/curl-8.7.1.tar.gz -C $BUILD_DIR
cd $BUILD_DIR/curl-8.7.1
./configure --prefix=$INSTALL_DIR --with-openssl=$INSTALL_DIR --with-zlib=$INSTALL_DIR --disable-shared --without-brotli --without-zstd
make install


mkdir -p $BUILD_DIR
cd $BUILD_DIR
curl -L -O https://sourceforge.net/projects/tcl/files/Tcl/8.6.14/tcl8.6.14-src.tar.gz
tar -xzvf tcl8.6.14-src.tar.gz
cd tcl8.6.14/unix
./configure --prefix=$INSTALL_DIR --disable-shared
make
make install
cd $BUILD_DIR
cmake .. \
  -DTCL_LIBRARY=$INSTALL_DIR/lib/libtcl8.6.a \
  -DTCL_INCLUDE_DIR=$INSTALL_DIR/include \
  -DCJSON_LIBRARY=$INSTALL_DIR/lib/libcjson.a \
  -DCJSON_INCLUDE_DIR=$INSTALL_DIR/include \
  -DZLIB_LIBRARY=$INSTALL_DIR/lib/libz.a \
  -DZLIB_INCLUDE_DIR=$INSTALL_DIR/include \
  -DCURL_INCLUDE_DIR=$INSTALL_DIR/include \
  -DCURL_LIBRARY=$INSTALL_DIR/lib/libcurl.a \
  -DOPENSSL_INCLUDE_DIR=$INSTALL_DIR/include \
  -DOPENSSL_LIBRARIES="$INSTALL_DIR/lib64/libssl.a;$INSTALL_DIR/lib64/libcrypto.a"
make

# sudo apt install musl-tools
# export CC=musl-gcc
# cmake -DCMAKE_TOOLCHAIN_FILE=$(pwd)/../toolchain/alpine.cmake -DTCL_LIBRARY=$BUILD_DIR/static-tcl8.6.14/lib/libtcl8.6.a   -DTCL_INCLUDE_DIR=$BUILD_DIR/static-tcl8.6.14/include   -DCJSON_LIBRARY=$BUILD_DIR/static-cjson-1.7.17/lib/libcjson.a   -DCJSON_INCLUDE_DIR=$BUILD_DIR/static-cjson-1.7.17/include   -DZLIB_LIBRARY=$BUILD_DIR/static-zlib-1.3.1/lib/libz.a -DZLIB_INCLUDE_DIR=$BUILD_DIR/static-zlib-1.3.1/include ..