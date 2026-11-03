# ttrek

The package manager for Tcl. This project is under active development.

## Rules and principles

- `ttrek` owns registry access, semantic version policy, USE flags, lockfiles, build scripts, and installation ordering.
- Dependency solving belongs to the standalone [satdepsolver-cpp](https://github.com/jerily/satdepsolver-cpp) C++17 library. Do not vendor another solver implementation here.
- Installation and update use the same provider and planner. Their default strategies remain `favored` and `latest`, respectively; `locked` prevents changing a locked candidate when that version is available.
- Linux release builds remain static. Preserve the existing range syntax, caret behavior (same major), and prerelease policy when changing the provider.

## Build

Requires C/C++ compilers with C++17, CMake 3.22.1+, Make, Bash, Perl, pkg-config,
curl, tar, Python 3 for tests, and the normal platform development tools. Rust and Cargo are no longer required.

```sh
./build-ttrek.sh
```

The script builds pinned zlib, OpenSSL, cJSON, curl, Tcl 9.1, libgit2, and libarchive
sources, installs the [satdepsolver 0.6.0](https://github.com/jerily/satdepsolver-cpp/releases/tag/v0.6.0)
CMake package, then builds and tests
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
and installs satdepsolver and rebuilds/tests ttrek.

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

### Linux release binaries

From the repository root, use Docker Buildx with a builder supporting
`linux/amd64` and `linux/arm64` (Docker Desktop includes emulation). Each build
runs the existing solver and ttrek tests before exporting a static executable.
Ubuntu 22.04 selects glibc; Alpine 3.23 selects musl. Build the four targets
sequentially on an 8 GB machine:

```sh
docker buildx build --platform linux/amd64 -f docker/Dockerfile.release \
  --build-arg BASE_IMAGE=ubuntu:22.04 --build-arg JOBS=2 \
  --output type=local,dest=dist/linux-amd64-glibc .
docker buildx build --platform linux/arm64 -f docker/Dockerfile.release \
  --build-arg BASE_IMAGE=ubuntu:22.04 --build-arg JOBS=2 \
  --output type=local,dest=dist/linux-arm64-glibc .
docker buildx build --platform linux/amd64 -f docker/Dockerfile.release \
  --build-arg BASE_IMAGE=alpine:3.23 --build-arg JOBS=2 \
  --output type=local,dest=dist/linux-amd64-musl .
docker buildx build --platform linux/arm64 -f docker/Dockerfile.release \
  --build-arg BASE_IMAGE=alpine:3.23 --build-arg JOBS=2 \
  --output type=local,dest=dist/linux-arm64-musl .
```

Each output directory contains `ttrek`. Docker's [local exporter](https://docs.docker.com/build/exporters/local-tar/)
copies only the executable from the final stage; no container needs to be created.
The build/install directories are isolated by image and target architecture.
Emulated builds can be slow. These are static Linux builds, not macOS binaries;
glibc's static resolver/dlopen limitations still apply.

### macOS release binaries

Install Xcode Command Line Tools (`xcode-select --install`) and Homebrew's
`cmake`, `pkg-config`, and `python` for each architecture you build.
[Homebrew uses separate prefixes](https://docs.brew.sh/FAQ) for ARM64
(`/opt/homebrew`) and Intel (`/usr/local`). Install Rosetta before the Intel
build, and install Intel Homebrew separately if needed.

Run these from the repository root. Keep the entire build and its tests in the
matching architecture shell, with CMake, CTest and Python from the same Homebrew
prefix. Python's `platform.machine()` must match the executable under test,
because the registry fixtures use that value. Separate build/install directories
also keep dependency sources and CMake caches from crossing architectures.

Native Apple Silicon:

```sh
arch -arm64 /bin/bash --noprofile --norc <<'SH'
set -eu
export PATH="/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin"
brew install cmake pkg-config python
export CC=/usr/bin/clang CXX=/usr/bin/clang++
export CMAKE=/opt/homebrew/bin/cmake CTEST=/opt/homebrew/bin/ctest
export PYTHON=/opt/homebrew/bin/python3
"$PYTHON" -c 'import platform; assert platform.machine() == "arm64"'
JOBS=2 TTREK_MAKE_THREADS=2 \
  TTREK_BUILD_DIR="$PWD/build/macos-arm64" \
  TTREK_INSTALL_DIR="$PWD/local-static/macos-arm64" ./build-ttrek.sh
mkdir -p dist/macos-arm64
cp build/macos-arm64/ttrek dist/macos-arm64/ttrek
file dist/macos-arm64/ttrek
SH
```

Intel macOS through Rosetta on Apple Silicon:

```sh
arch -x86_64 /bin/bash --noprofile --norc <<'SH'
set -eu
export PATH="/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin"
brew install cmake pkg-config python
export CC=/usr/bin/clang CXX=/usr/bin/clang++
export CMAKE=/usr/local/bin/cmake CTEST=/usr/local/bin/ctest
export PYTHON=/usr/local/bin/python3
"$PYTHON" -c 'import platform; assert platform.machine() == "x86_64"'
JOBS=2 TTREK_MAKE_THREADS=2 \
  TTREK_BUILD_DIR="$PWD/build/macos-x86_64" \
  TTREK_INSTALL_DIR="$PWD/local-static/macos-x86_64" ./build-ttrek.sh
mkdir -p dist/macos-x86_64
cp build/macos-x86_64/ttrek dist/macos-x86_64/ttrek
file dist/macos-x86_64/ttrek
SH
```

After both builds and their tests pass, optionally combine them into universal2:

```sh
mkdir -p dist/macos-universal2
lipo -create dist/macos-arm64/ttrek dist/macos-x86_64/ttrek \
  -output dist/macos-universal2/ttrek
lipo -verify_arch arm64 x86_64 dist/macos-universal2/ttrek
arch -arm64 dist/macos-universal2/ttrek help
arch -x86_64 dist/macos-universal2/ttrek help
```

macOS uses static third-party dependencies and dynamic system frameworks.
Use the same SDK and, if setting `MACOSX_DEPLOYMENT_TARGET`, the same supported
minimum macOS version for both builds. Packaging with `lipo` does not replace
testing each architecture on the macOS versions you intend to support.

## Run and preview

Registry requests default to `https://ttrek.sh` when no local registry is selected.
With `ENV=LOCAL`, registry, telemetry, fallback-download and release metadata
requests use `http://localhost:8080` for the local ttrek.sh server. Production
release downloads require HTTPS; LOCAL mode permits loopback HTTP for metadata.
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

`ttrek.json` also describes ttrek’s pinned static build. `build-ttrek.sh` remains available.

### Run installed package scripts

Recipes can define a `scripts` object mapping names to shell script strings.
Installation saves the exact version and scripts in
`ttrek-venv/ttrek-manifest.json`, alongside the package's installed file list:

```sh
ttrek install openacs@5.10.1
ttrek scripts -package openacs ls
ttrek scripts -package openacs describe init-local
ttrek run -package openacs -script init-local
ttrek run -package openacs -script start-local
```

Execution and inspection use installed metadata without registry access. Updates
replace cached scripts and uninstall removes them. Packages installed before
this support require `ttrek install -force PACKAGE` using a registry recipe
that supplies the scripts. A plain install of an already installed version does
not refresh its metadata. Use the same `-u` or `-g` mode for all commands when
working with those environments.

Without `-package`, script commands use the project's `ttrek.json` as before.
Package scripts support `ls` and `describe`; `add` and `del` edit project scripts
only. Installing a recipe does not automatically execute its named scripts or
copy them into the project spec.

Scripts run in the installation directory using `$SHELL`, honoring an executable
script's shebang. `PROJECT_HOME` names the project root and `PROJECT_INSTALL_DIR`
names the installation prefix. `PATH` and `LD_LIBRARY_PATH` select that environment.
Keep databases, uploads and other runtime data outside `ttrek-venv`.

### Generate a standalone build script

```sh
ttrek gen-script > build.sh
ttrek gen-script -shell sh -static jinja2-tcl > build-static.sh
bash build.sh
```

`gen-script` defaults to Bash, supports `sh`, and replaces `install -bootstrap`.
It resolves dependencies, embeds patches, and writes only the completed script to
stdout. Generation is read-only; hooks and builds run when you execute the script.
The script installs into `build/install` beside itself and needs no ttrek binary.
It rebuilds packages on each run. Use `-strategy latest|favored|locked` to select versions.

`-static` enables the `+static` USE flag. Recipes use `"if": "+static"` for optional
static build settings; without the flag, their normal defaults apply.
Static Tcl extensions are archives for embedding; an application must register their init functions.

Use a local registry directory instead of the registry API:

```sh
ttrek gen-script -registry ../ttrek.sh/registry > build.generated.sh
ttrek -registry ../ttrek.sh/registry install -y
ttrek init -registry ../ttrek.sh/registry
```

`-registry` also works with `pretend` and `update`. The directory contains
`package/version/ttrek.json` and optional `.diff` patches. Local mode sends no
registry or telemetry requests; source downloads still follow recipe URLs.
`init -registry` saves `registry` in `ttrek-lock.json` for later commands.
An explicit option overrides it without changing the saved value. Relative saved
paths resolve from the project directory.

### Sync the local registry

```sh
ttrek sync                         # ~/.local/ttrek-registry
ttrek sync ./registry              # explicit destination
ttrek sync .                       # current directory must be empty or the registry checkout
```

`sync` requires Git and clones `https://github.com/jerily/ttrek-registry.git`.
Later runs fast-forward a clean `main` checkout of that repository. Local edits,
another repository, another branch, and divergent history are rejected without
overwriting the checkout. Network errors from an explicit sync are reported.

Registry commands select the explicit `-registry` option first, then the saved
project registry, then `~/.local/ttrek-registry` after a successful `ttrek sync`,
then the website. Normal commands never create or clone the default directory;
an absent or unmarked directory leaves website access enabled. An explicitly
selected local registry remains authoritative: missing packages do not fall back
to the website.
Automatic default selection is not saved in project lockfiles, so projects stay
portable and fall back to the website if that checkout is later removed.

Sync records `.git/ttrek-last-sync`. After 24 hours, commands using the default
checkout print a refresh reminder to stderr with `ttrek sync`. This reports the
age of the last successful sync, without requiring a network check or claiming
to know GitHub's current commit. Stale data remains usable offline. `gen-script`
continues to emit only the script on stdout.

### Update the ttrek executable

```sh
ttrek self-update -version         # current version; no network access
ttrek self-update -check           # check without installing
ttrek self-update                  # download, verify and replace this executable
```

`update` continues to update project packages. `self-update` reads
`https://ttrek.sh/releases/latest/<target>`, selects the running executable's
architecture and libc, and uses the signed download URL from the metadata.
It verifies SHA-256 and the downloaded program's reported version before an
atomic replacement in the installation directory. Errors leave the existing
binary intact, and temporary files are removed. Symlinks remain intact; their
resolved target is replaced. The directory must be writable, and no sudo is run.
macOS universal2 installations update to the currently executing architecture.

Successful interactive `init`, `install`, `update`, `uninstall`, `ls`, `run`, and
`sync` commands also check at most once per day, with a one-second request
timeout. Connection and metadata failures are ignored; notices go to stderr.
Noninteractive commands, `pretend`, and `gen-script` do not check. Set
`TTREK_NO_UPDATE_CHECK=1` to disable automatic checks. Check timestamps live under
`~/.local/state/ttrek/`; this does not create the local registry directory.

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

`gen_script` exercises the CLI with a fixture registry, builds shared and static
libraries, and checks patches, dependency order, shell isolation and generation errors.
`local_registry` checks directory registries, saved settings, installs and updates.
Both tests use temporary home directories and local source archives. CMake prefers
the dependency prefix's `bin/curl` for these tests; set `-DTTREK_TEST_CURL=/path/to/curl`
to select another executable. Build failures include their logs in CTest output.

With the website available (or a synced default registry), the optional read-only smoke test verifies
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

Self-update accepts HTTPS metadata and downloads, verifies the published checksum
and version, and replaces the executable only after validation. Registry sync
uses Git's fast-forward checks and rejects dirty or unrelated checkouts.

Resolution validates registry JSON, owns metadata strings, and handles HTTP errors.
C++ resolution exceptions are converted to Tcl errors. Preview skips installation
and telemetry; tests use disposable projects. Dependency builds use version-pinned
HTTPS downloads, and the solver uses a pinned release.

### Pending

Registry authentication, signed package manifests, resource limits, and downloaded
archive checksum verification are not added in this migration. The existing range
and prerelease semantics are preserved rather than replaced with npm semantics.
Large-registry performance and non-Linux platform validation remain follow-up work.
