#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
BUILD_DIR=${TTREK_BUILD_DIR:-"$SCRIPT_DIR/build"}
INSTALL_DIR=${TTREK_INSTALL_DIR:-"$SCRIPT_DIR/local-static"}
CMAKE=${CMAKE:-cmake}
CTEST=${CTEST:-ctest}
CURL=${CURL:-curl}
JOBS=${JOBS:-$(getconf _NPROCESSORS_ONLN)}
BUILD_TYPE=${BUILD_TYPE:-Release}

# Keep dependencies pinned. No Rust toolchain is needed by satdepsolver.
ZLIB_VERSION=1.3.2
OPENSSL_VERSION=3.5.9
CJSON_VERSION=1.7.19
CURL_VERSION=8.23.0
TCL_VERSION=9.1.0
LIBGIT2_VERSION=1.9.7
LIBARCHIVE_VERSION=3.8.11
SATDEPSOLVER_REVISION=691db3251ab85a692582e661d2e578135419c38e

mkdir -p "$BUILD_DIR" "$INSTALL_DIR"
BUILD_DIR=$(cd "$BUILD_DIR" && pwd)
INSTALL_DIR=$(cd "$INSTALL_DIR" && pwd)
DOWNLOAD_DIR="$BUILD_DIR/downloads"
SOURCE_DIR="$BUILD_DIR/source"
mkdir -p "$DOWNLOAD_DIR" "$SOURCE_DIR"

# Download atomically so an interrupted run never leaves a valid-looking archive.
fetch_source() {
    local name=$1 url=$2
    if [[ ! -f "$DOWNLOAD_DIR/$name.tar.gz" ]]; then
        "$CURL" --fail --location --retry 3 --output "$DOWNLOAD_DIR/$name.tar.gz.part" "$url"
        mv "$DOWNLOAD_DIR/$name.tar.gz.part" "$DOWNLOAD_DIR/$name.tar.gz"
    fi
    if [[ ! -d "$SOURCE_DIR/$name" ]]; then
        mkdir -p "$SOURCE_DIR/$name.unpack"
        tar -xzf "$DOWNLOAD_DIR/$name.tar.gz" --strip-components=1 -C "$SOURCE_DIR/$name.unpack"
        mv "$SOURCE_DIR/$name.unpack" "$SOURCE_DIR/$name"
    fi
}

cmake_dependency() {
    local name=$1
    shift
    "$CMAKE" -S "$SOURCE_DIR/$name" -B "$BUILD_DIR/dependencies/$name" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
        -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_PREFIX_PATH="$INSTALL_DIR" \
        -DBUILD_SHARED_LIBS=OFF -DCMAKE_POLICY_VERSION_MINIMUM=3.5 "$@"
    "$CMAKE" --build "$BUILD_DIR/dependencies/$name" --parallel "$JOBS"
    "$CMAKE" --install "$BUILD_DIR/dependencies/$name"
}

# Use only this prefix when configuring static dependencies.
export PKG_CONFIG_PATH="$INSTALL_DIR/lib/pkgconfig"
export CPPFLAGS="-I$INSTALL_DIR/include ${CPPFLAGS:-}"
export LDFLAGS="-L$INSTALL_DIR/lib ${LDFLAGS:-}"

if [[ ${TTREK_SKIP_DEPS:-0} != 1 ]]; then
    fetch_source "zlib-$ZLIB_VERSION" "https://github.com/madler/zlib/releases/download/v$ZLIB_VERSION/zlib-$ZLIB_VERSION.tar.gz"
    (cd "$SOURCE_DIR/zlib-$ZLIB_VERSION"
     ./configure --prefix="$INSTALL_DIR" --static
     make -j "$JOBS"
     make install)

    fetch_source "openssl-$OPENSSL_VERSION" "https://github.com/openssl/openssl/releases/download/openssl-$OPENSSL_VERSION/openssl-$OPENSSL_VERSION.tar.gz"
    (cd "$SOURCE_DIR/openssl-$OPENSSL_VERSION"
     ./Configure --prefix="$INSTALL_DIR" --libdir=lib no-shared no-pinshared no-tests
     make -j "$JOBS"
     make install_sw)

    fetch_source "cjson-$CJSON_VERSION" "https://github.com/DaveGamble/cJSON/archive/refs/tags/v$CJSON_VERSION.tar.gz"
    cmake_dependency "cjson-$CJSON_VERSION" -DENABLE_CJSON_TEST=OFF

    fetch_source "curl-$CURL_VERSION" "https://curl.se/download/curl-$CURL_VERSION.tar.gz"
    (cd "$SOURCE_DIR/curl-$CURL_VERSION"
     ./configure --prefix="$INSTALL_DIR" --with-openssl="$INSTALL_DIR" --with-zlib="$INSTALL_DIR" \
         --disable-shared --enable-static --without-brotli --without-zstd --without-nghttp2 \
         --without-nghttp3 --without-ngtcp2 --without-libpsl --without-libidn2 \
         --without-libssh2 --without-librtmp --without-gssapi --disable-ldap --disable-ldaps
     make -j "$JOBS"
     make install)

    fetch_source "tcl-$TCL_VERSION" "https://github.com/tcltk/tcl/archive/refs/tags/core-${TCL_VERSION//./-}.tar.gz"
    (cd "$SOURCE_DIR/tcl-$TCL_VERSION/unix"
     ./configure --prefix="$INSTALL_DIR" --disable-shared --with-zlib="$INSTALL_DIR"
     make -j "$JOBS"
     make install)

    fetch_source "libgit2-$LIBGIT2_VERSION" "https://github.com/libgit2/libgit2/archive/refs/tags/v$LIBGIT2_VERSION.tar.gz"
    cmake_dependency "libgit2-$LIBGIT2_VERSION" -DBUILD_TESTS=OFF -DBUILD_CLI=OFF \
        -DUSE_THREADS=OFF -DUSE_NSEC=OFF -DUSE_HTTPS=OFF -DUSE_SSH=OFF -DUSE_ICONV=OFF -DREGEX_BACKEND=builtin

    fetch_source "libarchive-$LIBARCHIVE_VERSION" "https://github.com/libarchive/libarchive/releases/download/v$LIBARCHIVE_VERSION/libarchive-$LIBARCHIVE_VERSION.tar.gz"
    cmake_dependency "libarchive-$LIBARCHIVE_VERSION" \
        -DENABLE_ACL=OFF -DENABLE_CPIO=OFF -DENABLE_TAR=OFF -DENABLE_CAT=OFF -DENABLE_UNZIP=OFF \
        -DENABLE_TEST=OFF -DENABLE_XATTR=OFF -DENABLE_ICONV=OFF -DENABLE_COVERAGE=OFF \
        -DENABLE_LIBB2=OFF -DENABLE_LZ4=OFF -DENABLE_LZO=OFF -DENABLE_LZMA=OFF -DENABLE_ZSTD=OFF \
        -DENABLE_ZLIB=ON -DENABLE_BZip2=OFF -DENABLE_LIBXML2=OFF -DENABLE_EXPAT=OFF \
        -DENABLE_PCREPOSIX=OFF -DENABLE_PCRE2POSIX=OFF -DENABLE_CNG=OFF -DENABLE_OPENSSL=ON
fi

if [[ -z ${SATDEPSOLVER_SOURCE_DIR:-} ]]; then
    fetch_source "satdepsolver-$SATDEPSOLVER_REVISION" "https://github.com/jerily/satdepsolver-cpp/archive/$SATDEPSOLVER_REVISION.tar.gz"
    SATDEPSOLVER_SOURCE_DIR="$SOURCE_DIR/satdepsolver-$SATDEPSOLVER_REVISION"
fi

SATDEPSOLVER_SOURCE_DIR=$(cd "$SATDEPSOLVER_SOURCE_DIR" && pwd)
# Local-checkout and pinned-source builds must not reuse each other's CMake cache.
SOLVER_SOURCE_KEY=$(printf '%s' "$SATDEPSOLVER_SOURCE_DIR" | cksum | cut -d ' ' -f 1)
SOLVER_BUILD_DIR="$BUILD_DIR/dependencies/satdepsolver-$SOLVER_SOURCE_KEY"

"$CMAKE" -S "$SATDEPSOLVER_SOURCE_DIR" -B "$SOLVER_BUILD_DIR" \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" -DCMAKE_INSTALL_LIBDIR=lib \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DSATDEPSOLVER_BUILD_TESTS=ON
"$CMAKE" --build "$SOLVER_BUILD_DIR" --parallel "$JOBS"
"$CTEST" --test-dir "$SOLVER_BUILD_DIR" --output-on-failure
"$CMAKE" --install "$SOLVER_BUILD_DIR"

# Clear old dependency paths (including Tcl 8.6 and lib64 OpenSSL) on reruns.
"$CMAKE" -S "$SCRIPT_DIR" -B "$BUILD_DIR" -U '*_LIBRARY' -U OPENSSL_LIBRARIES \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" -DCMAKE_PREFIX_PATH="$INSTALL_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DBUILD_TESTING=ON -DSATDEPSOLVER_SOURCE_DIR= \
    -Dsatdepsolver_DIR="$INSTALL_DIR/lib/cmake/satdepsolver"
"$CMAKE" --build "$BUILD_DIR" --parallel "$JOBS"
"$CTEST" --test-dir "$BUILD_DIR" --output-on-failure
