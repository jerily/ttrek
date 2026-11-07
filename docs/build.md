# Build ttrek from source

[Documentation](readme.md) · [Project overview](../readme.md)

Run these commands from the root of the ttrek source checkout.
For platform setup commands, see [Install build tools](install.md#install-build-tools).

Requires C/C++ compilers with C++17, CMake 3.22.1+, Make, Bash, Perl, pkg-config,
curl, tar, Python 3 for tests, and the normal platform development tools. Rust and Cargo are no longer required.

```sh
./build-ttrek.sh
```

The script builds pinned zlib, OpenSSL, cJSON, curl, Tcl 9.1, libgit2, and libarchive
sources, installs the [satdepsolver 0.6.0](https://github.com/jerily/satdepsolver-cpp/releases/tag/v0.6.0)
CMake package and ttrie 0.1.1/ttrigrams 0.1.0 static C libraries, then builds and tests
`build/ttrek`. Downloads and extracted sources are cached for reruns. OpenSSL is
installed under `lib`, and dependency configuration uses the local static prefix.
Tcl 9.0 static installations remain usable for manual CMake builds.

Dependency release sources: [zlib](https://zlib.net/),
[OpenSSL](https://www.openssl-library.org/source/), [cJSON](https://github.com/DaveGamble/cJSON/releases),
[curl](https://curl.se/download/), [Tcl](https://github.com/tcltk/tcl/releases),
[libgit2](https://github.com/libgit2/libgit2/releases), and
[libarchive](https://github.com/libarchive/libarchive/releases).

For joint development against a solver checkout:

```sh
SATDEPSOLVER_SOURCE_DIR=../satdepsolver-cpp ./build-ttrek.sh
```

Other build controls: `JOBS` (default: 2, suitable for an 8 GB Mac), `BUILD_TYPE`,
`TTREK_BUILD_DIR`, `TTREK_INSTALL_DIR`, `CMAKE`, `CTEST`, `CURL`, and `PYTHON`
(the Python 3 test interpreter). `TTREK_SKIP_DEPS=1` skips building the seven C dependencies
when a complete compatible static prefix already exists; it still builds, tests,
and installs satdepsolver, ttrie and ttrigrams and rebuilds/tests ttrek.
Set `TTRIE_SOURCE_DIR=/path/to/ttrie` to use a local trie checkout.
Set `TTRIGRAMS_SOURCE_DIR=/path/to/ttrigrams` to use a local trigram checkout.

On macOS, `build-ttrek.sh` uses `MACOSX_DEPLOYMENT_TARGET` for every dependency
and the final executable, defaulting to the build machine's macOS version.
Set it explicitly when targeting an older supported macOS version. CMake receives
the same value explicitly, including when reconfiguring an existing build tree.

If the linker reports objects built for a newer macOS version than the link
target, rebuild all dependencies in fresh build **and** install directories.
Changing the target does not rebuild cached Autoconf objects automatically.
For example, to target macOS 14.0 without deleting an existing build:

```sh
MACOSX_DEPLOYMENT_TARGET=14.0 \
  TTREK_BUILD_DIR="$PWD/build/macos-14-clean" \
  TTREK_INSTALL_DIR="$PWD/local-static/macos-14-clean" \
  ./build-ttrek.sh
```

Use previously unused directory names and leave `TTREK_SKIP_DEPS` unset for
this rebuild. SDK headers can be newer than the deployment target; compiled
dependencies must support the minimum version selected for the executable.

To configure manually after installing dependencies:

```sh
cmake -S . -B build -DCMAKE_INSTALL_PREFIX="$PWD/local-static" \
  -DCMAKE_PREFIX_PATH="$PWD/local-static"
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

CMake normally uses `find_package(satdepsolver 0.6 CONFIG REQUIRED)` and
`satdepsolver::satdepsolver`. Set `-DSATDEPSOLVER_SOURCE_DIR=/path/to/checkout`
for an explicit `add_subdirectory` integration. No network download happens at
CMake configure time. The build script downloads the tagged satdepsolver 0.6.0 release.
The trie uses `find_package(ttrie 0.1 CONFIG REQUIRED)` and `ttrie::core`; the
build script downloads its 0.1.1 release and builds it without Tcl bindings.

`ttrek.json` also describes ttrek’s pinned static build. `build-ttrek.sh` remains available.

See [development](development.md) for implementation notes and test coverage,
and [releases](releases.md) for platform binaries and upload instructions.
