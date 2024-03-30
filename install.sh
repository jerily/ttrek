#!/bin/bash

set -eo pipefail # exit on error

export SCRIPT_DIR=$(dirname $(readlink -f $0))
INSTALL_DIR=$SCRIPT_DIR/local
echo "Installing to $INSTALL_DIR"

BUILD_DIR=$SCRIPT_DIR/build
mkdir -p $BUILD_DIR

BUILD_LOG_DIR=$BUILD_DIR/logs
mkdir -p $BUILD_LOG_DIR

export PKG_CONFIG_PATH=$INSTALL_DIR/lib/pkgconfig

# Install the tools
if false; then
sudo apt-get install cmake ;# perl gcc zip unzip tar curl c++ make
fi

# zlib
if true; then
  curl -L -o zlib-1.3.1.tar.gz --output-dir $BUILD_DIR https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz
  tar -xvf $BUILD_DIR/zlib-1.3.1.tar.gz -C $BUILD_DIR
  cd $BUILD_DIR/zlib-1.3.1
  ./configure --prefix=$INSTALL_DIR > $BUILD_LOG_DIR/zlib-configure.log 2>&1
  make install > $BUILD_LOG_DIR/zlib-install.log 2>&1
fi

# openssl
if true; then
  curl -L -O --output-dir $BUILD_DIR/ https://www.openssl.org/source/openssl-3.2.1.tar.gz
  tar -xvf $BUILD_DIR/openssl-3.2.1.tar.gz -C $BUILD_DIR
  cd $BUILD_DIR/openssl-3.2.1
  ./Configure --prefix=$INSTALL_DIR > $BUILD_LOG_DIR/openssl-configure.log 2>&1
  make install > $BUILD_LOG_DIR/openssl-install.log 2>&1
fi

#curl
if true; then
  curl -L -O --output-dir $BUILD_DIR https://curl.se/download/curl-8.7.1.tar.gz
  tar -xvf $BUILD_DIR/curl-8.7.1.tar.gz -C $BUILD_DIR
  cd $BUILD_DIR/curl-8.7.1
  ./configure --prefix=$INSTALL_DIR --with-openssl=$INSTALL_DIR --with-zlib=$INSTALL_DIR > $BUILD_LOG_DIR/curl-configure.log 2>&1
  make install > $BUILD_LOG_DIR/curl-install.log 2>&1
fi

# tcl
if true; then
  curl -L -O --output-dir $BUILD_DIR http://prdownloads.sourceforge.net/tcl/tcl9.0b1-src.tar.gz
  tar -xvf $BUILD_DIR/tcl9.0b1-src.tar.gz -C $BUILD_DIR
  cd $BUILD_DIR/tcl9.0b1/unix
  ./configure  --prefix=$INSTALL_DIR > $BUILD_LOG_DIR/tcl-configure.log 2>&1
  make install > $BUILD_LOG_DIR/tcl-install.log 2>&1
fi

# twebserver
if true; then
  git -C $BUILD_DIR clone https://github.com/jerily/twebserver.git
  cd $BUILD_DIR/twebserver
  mkdir build
  cd build
  cmake .. \
    -DTCL_LIBRARY_DIR=$INSTALL_DIR/lib \
    -DTCL_INCLUDE_DIR=$INSTALL_DIR/include \
    -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR \
    -DCMAKE_PREFIX_PATH=$INSTALL_DIR/ > $BUILD_LOG_DIR/twebserver-configure.log 2>&1
  make install > $BUILD_LOG_DIR/twebserver-install.log 2>&1
fi

# tdom
if true; then
  curl -L -o tdom-0.9.3-src.tar.gz --output-dir $BUILD_DIR http://tdom.org/downloads/tdom-0.9.3-src.tar.gz
  tar -xzf $BUILD_DIR/tdom-0.9.3-src.tar.gz -C $BUILD_DIR
  cd $BUILD_DIR/tdom-0.9.3-src/unix
  ../configure --prefix=$INSTALL_DIR > $BUILD_LOG_DIR/tdom-configure.log 2>&1
  make install > $BUILD_LOG_DIR/tdom-install.log 2>&1
fi

# thtml
if true; then
  git -C $BUILD_DIR clone https://github.com/jerily/thtml.git
  cd $BUILD_DIR/thtml
  mkdir build
  cd build
  cmake .. \
    -DTCL_LIBRARY_DIR=$INSTALL_DIR/lib \
    -DTCL_INCLUDE_DIR=$INSTALL_DIR/include \
    -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR \
    -DCMAKE_PREFIX_PATH=$INSTALL_DIR/ > $BUILD_LOG_DIR/thtml-configure.log 2>&1
  make install > $BUILD_LOG_DIR/thtml-install.log 2>&1
fi

# abseil-cpp
if true; then
  curl -L -o abseil-cpp-20230125.0.tar.gz --output-dir $BUILD_DIR https://github.com/abseil/abseil-cpp/archive/refs/tags/20230125.0.tar.gz
  tar -xzf $BUILD_DIR/abseil-cpp-20230125.0.tar.gz -C $BUILD_DIR
  cd $BUILD_DIR/abseil-cpp-20230125.0
  mkdir build
  cd build
  cmake .. \
    -DBUILD_SHARED_LIBS=ON \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_STANDARD=14 \
    -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR \
    -DCMAKE_PREFIX_PATH=$INSTALL_DIR/ > $BUILD_LOG_DIR/abseil-cpp-configure.log 2>&1
  make install > $BUILD_LOG_DIR/abseil-cpp-install.log 2>&1
fi

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

# tink-cc and tink-tcl
if true; then
  git -C $BUILD_DIR clone https://github.com/jerily/tink-tcl.git
  cd $BUILD_DIR/tink-tcl
  export TINK_TCL_DIR=`pwd`

  curl -L -o tink-cc-2.1.1.tar.gz --output-dir $BUILD_DIR https://github.com/tink-crypto/tink-cc/archive/refs/tags/v2.1.1.tar.gz
  tar -xzf $BUILD_DIR/tink-cc-2.1.1.tar.gz -C $BUILD_DIR
  cd $BUILD_DIR/tink-cc-2.1.1
  patch -p1 < ${TINK_TCL_DIR}/tink-cc-2.1.1.diff > $BUILD_LOG_DIR/tink-cc-patch.log 2>&1
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

  cd ${TINK_TCL_DIR}
  mkdir build
  cd build
  cmake .. \
    -DTCL_LIBRARY_DIR=$INSTALL_DIR/lib \
    -DTCL_INCLUDE_DIR=$INSTALL_DIR/include \
    -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR \
    -DCMAKE_PREFIX_PATH=$INSTALL_DIR/ > $BUILD_LOG_DIR/tink-tcl-configure.log 2>&1
  make install > $BUILD_LOG_DIR/tink-tcl-install.log 2>&1

fi

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


# tsession
if true; then
  git -C $BUILD_DIR clone https://github.com/jerily/tsession.git
  cd $BUILD_DIR/tsession
  make install PREFIX=$INSTALL_DIR > $BUILD_LOG_DIR/tsession-install.log 2>&1
fi

# ksuid-tcl
if true; then
  curl -L -o ksuid-tcl-1.0.3.tar.gz --output-dir $BUILD_DIR https://github.com/jerily/ksuid-tcl/archive/refs/tags/v1.0.3.tar.gz
  tar -xzf $BUILD_DIR/ksuid-tcl-1.0.3.tar.gz -C $BUILD_DIR
  cd $BUILD_DIR/ksuid-tcl-1.0.3
  mkdir build
  cd build
  cmake .. \
    -DTCL_LIBRARY_DIR=$INSTALL_DIR/lib \
    -DTCL_INCLUDE_DIR=$INSTALL_DIR/include \
    -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR \
    -DCMAKE_PREFIX_PATH=$INSTALL_DIR/ > $BUILD_LOG_DIR/ksuid-tcl-configure.log 2>&1
  make install > $BUILD_LOG_DIR/ksuid-tcl-install.log 2>&1
fi