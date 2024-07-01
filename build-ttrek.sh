#!/bin/bash


set -eo pipefail # exit on error
export SCRIPT_DIR=$(dirname $(readlink -f $0))
BUILD_DIR=$SCRIPT_DIR/build
INSTALL_DIR=$SCRIPT_DIR/local-static

GNUMAKEFLAGS=-j9
export GNUMAKEFLAGS

curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y

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
./Configure --prefix=$INSTALL_DIR -no-shared -no-pinshared no-docs
make
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
./configure --prefix=$INSTALL_DIR --with-openssl=$INSTALL_DIR --with-zlib=$INSTALL_DIR --disable-shared --without-brotli --without-zstd --without-nghttp2 --disable-ldap --without-libidn2
make install

mkdir -p $BUILD_DIR
cd $BUILD_DIR
# the package Threads from sourceforge's archive cannot be built if
# the Info Zip binary is not present in built environment:
#
# creating libthread.vfs/thread_library (prepare compression)
# creating libthread3.0b2.zip from libthread.vfs/thread_library
# cd libthread.vfs && /w/repositories/jerily/ttrek/build/tcl9.0b2/unix/pkgs8/thread3.0b2/minizip -o -r ../libthread3.0b2.zip *
# /bin/bash: line 5: /w/repositories/jerily/ttrek/build/tcl9.0b2/unix/pkgs8/thread3.0b2/minizip: No such file or directory
# make[1]: *** [Makefile:233: libthread3.0b2.zip] Error 127
#
#curl -L -O https://sourceforge.net/projects/tcl/files/Tcl/9.0b2/tcl9.0b2-src.tar.gz
#tar -xzvf tcl9.0b2-src.tar.gz
#cd tcl9.0b2/unix
curl -L -O https://github.com/tcltk/tcl/archive/refs/tags/core-9-0-b2.tar.gz
tar -xzvf core-9-0-b2.tar.gz
cd tcl-core-9-0-b2/unix
./configure --prefix=$INSTALL_DIR --disable-shared
make
make install

mkdir -p $BUILD_DIR
cd $BUILD_DIR
curl -L -o libgit2-1.8.1.tar.gz https://github.com/libgit2/libgit2/archive/refs/tags/v1.8.1.tar.gz
tar -xzvf libgit2-1.8.1.tar.gz
cd libgit2-1.8.1
mkdir build
cd build
cmake .. -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTS=OFF -DBUILD_CLI=OFF -DUSE_THREADS=OFF -DUSE_NSEC=OFF -DUSE_HTTPS=OFF -DUSE_ICONV=OFF -DREGEX_BACKEND=builtin
cmake --build . --target install

cd $BUILD_DIR
cmake .. -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR
GNUMAKEFLAGS= make

# sudo apt install musl-tools
# export CC=musl-gcc
# cmake -DCMAKE_TOOLCHAIN_FILE=$(pwd)/../toolchain/alpine.cmake -DTCL_LIBRARY=$BUILD_DIR/static-tcl8.6.14/lib/libtcl8.6.a   -DTCL_INCLUDE_DIR=$BUILD_DIR/static-tcl8.6.14/include   -DCJSON_LIBRARY=$BUILD_DIR/static-cjson-1.7.17/lib/libcjson.a   -DCJSON_INCLUDE_DIR=$BUILD_DIR/static-cjson-1.7.17/include   -DZLIB_LIBRARY=$BUILD_DIR/static-zlib-1.3.1/lib/libz.a -DZLIB_INCLUDE_DIR=$BUILD_DIR/static-zlib-1.3.1/include ..
