# ttrek

The package manager for Tcl. This project is under active development.

## Rules and principles

- `ttrek` owns registry access, semantic version policy, USE flags, lockfiles, build scripts, and installation ordering.
- Dependency solving belongs to the standalone [satdepsolver-cpp](https://github.com/jerily/satdepsolver-cpp) C++17 library. Do not vendor another solver implementation here.
- Installation and update use the same provider and planner. Their default strategies remain `favored` and `latest`, respectively; `locked` prevents changing a locked candidate when that version is available.
- Linux release builds remain static. Preserve the existing range syntax, caret behavior (same major), and prerelease policy when changing the provider.

## Build

Requires C/C++ compilers with C++17, CMake 3.22.1+, Make, Bash, Perl, pkg-config,
curl, tar, and the normal platform development tools. Rust and Cargo are no longer required.

```sh
./build-ttrek.sh
```

The script builds pinned zlib, OpenSSL, cJSON, curl, Tcl 9.1, libgit2, and libarchive
sources, installs the pinned satdepsolver CMake package, then builds and tests
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

Other build controls: `JOBS`, `BUILD_TYPE`, `TTREK_BUILD_DIR`, `TTREK_INSTALL_DIR`,
`CMAKE`, `CTEST`, and `CURL`. `TTREK_SKIP_DEPS=1` skips building the seven C dependencies
when a complete compatible static prefix already exists; it still builds, tests,
and installs satdepsolver and rebuilds/tests ttrek.

To configure manually after installing dependencies:

```sh
cmake -S . -B build -DCMAKE_INSTALL_PREFIX="$PWD/local-static" \
  -DCMAKE_PREFIX_PATH="$PWD/local-static"
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

CMake normally uses `find_package(satdepsolver 0.6 CONFIG REQUIRED)` and
`satdepsolver::satdepsolver`. Set `-DSATDEPSOLVER_SOURCE_DIR=/path/to/checkout`
for an explicit `add_subdirectory` integration. No network download happens at
CMake configure time. The build script pins a solver commit so its behavior does
not change when an upstream branch moves.

## Run and preview

Run [ttrek.sh](https://github.com/jerily/ttrek.sh) at `http://localhost:8080`.
Create a project directory and run:

```sh
/path/to/ttrek/build/ttrek init
/path/to/ttrek/build/ttrek pretend tjson twebserver
/path/to/ttrek/build/ttrek install tjson twebserver
/path/to/ttrek/build/ttrek update
```

`pretend` reads the project's `ttrek.json` and optional `ttrek-lock.json`, resolves
against the live registry, and prints the same installation plan as `install`.
It does not create a venv, modify project files, run hooks, install packages,
or initialize/send telemetry. It supports `-force` and
`-strategy latest|favored|locked`; use `-strategy latest` to preview update behavior.
With no package arguments it resolves existing project requirements.
Resolution conflicts and malformed/failed metadata requests return a nonzero exit.

## Solver integration

`PackageDatabase.h` implements `DependencyProvider<Range<Pack>>` and owns
package metadata. `ttrek_solver.cc` calls `solve_packages` and retains the existing
execution planner and installer. The provider uses IDs from one shared solver pool,
returns owned candidates and dependency lists, and translates failures through
`DisplayUnsat`. Registry strings and parsed semantic versions own their storage.

The solver copies its provider. After solving, ttrek retrieves that populated copy
and records dependencies only for selected versions. Dependencies of rejected
candidates do not affect installation order. The provider retains existing USE
flag filtering and version strategy decisions. No Rust FFI types or generated
binding headers remain in the build.

Migration order: merge the standalone library PR, then this dependent consumer PR.
The webserver's embedded solver copy is not modified by these PRs.

## Tests

`package_solver` tests the production provider against a local JSON fixture:
backtracking, dependency order, constraints, locks, favored/latest selection,
USE flags, conflict messages, transport/metadata errors, and version ownership.
Assertions stay enabled in Release. Debug builds enable address and undefined
behavior sanitizers.

With the local registry running, the optional read-only smoke test verifies
`tjson@1.0.26`, `twebserver@1.47.54`, a Tcl 8.6 pin, unsatisfiable/missing packages,
and that preview leaves the project untouched:

```sh
python3 src/sat-solver/tests/registry_smoke.py build/ttrek
```

## Security

### Surface

Registry metadata and package build instructions are external inputs. Installation
runs scripts from the configured registry with the invoking user's privileges.

### Protections

Resolution validates registry JSON, owns metadata strings, and handles HTTP errors.
C++ resolution exceptions are converted to Tcl errors. Preview skips installation
and telemetry; tests use disposable projects. Dependency builds use version-pinned
HTTPS downloads, and the solver uses a pinned commit.

### Pending

Registry authentication, signed package manifests, resource limits, and downloaded
archive checksum verification are not added in this migration. The existing range
and prerelease semantics are preserved rather than replaced with npm semantics.
Large-registry performance and non-Linux platform validation remain follow-up work.
