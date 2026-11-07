# Prepare and publish releases

[Documentation](readme.md) · [Project overview](../readme.md)

These instructions are for release maintainers. To install ttrek as a user,
see [Install ttrek](install.md). Run the commands below from the repository root;
relative paths refer to that directory.

## Linux release binaries

`bin/prepare-linux.sh` uses Docker Buildx and the existing build/tests in
`docker/Dockerfile.release`. Run each target on a native builder or one with
emulation for the requested architecture. Ubuntu 22.04 selects glibc; Alpine
3.23 selects musl. Each script exports `dist/<target>/ttrek`:

```sh
ttrek run -script prepare-linux-amd64-glibc
ttrek run -script prepare-linux-arm64-glibc
ttrek run -script prepare-linux-amd64-musl
ttrek run -script prepare-linux-arm64-musl
# Without an installed ttrek:
./bin/prepare-linux.sh arm64 musl
```

`JOBS` defaults to 2. Run targets sequentially on a small machine. Docker's
local exporter copies only the executable; build/install directories remain
isolated by image and architecture. Foreign architectures need a suitable
builder or emulation (included in Docker Desktop), which can be slow.
Musl builds are fully static. Glibc builds use the host's libc (glibc 2.35 or
newer, matching the Ubuntu 22.04 build baseline), with static third-party and
C++ runtime libraries. This keeps hostname lookups compatible with the host's
NSS modules.

## macOS release binaries

Install Xcode Command Line Tools and Homebrew's `cmake`, `pkg-config`, and
`python` for each architecture. ARM64 tools use `/opt/homebrew`; Intel tools
use `/usr/local`. Intel builds on Apple Silicon also require Rosetta.

```sh
ttrek run -script prepare-macos-arm64
ttrek run -script prepare-macos-x86_64
# Without an installed ttrek (defaults to the current architecture):
./bin/prepare-macos.sh
```

`bin/prepare-macos.sh` requires macOS and runs the full build and tests in the
selected architecture, using its Homebrew tools and a matching Python
interpreter. Outputs go to `dist/macos-arm64/ttrek` or
`dist/macos-x86_64/ttrek`. `JOBS` defaults to 2.

Without a matching Homebrew installation, provide `cmake`, `ctest`, `python3`,
and `pkg-config` in `build/tools-<architecture>/bin`. The wrapper uses this
local toolset when its `cmake` is present. Tools must run under the selected
architecture (universal binaries work too). `CMAKE`, `CTEST`, and `PYTHON`
can override the corresponding executable paths.

The wrapper exports `MACOSX_DEPLOYMENT_TARGET` to the full build, defaulting to
the build machine's macOS version. Select it explicitly for release builds:

```sh
MACOSX_DEPLOYMENT_TARGET=14.0 ./bin/prepare-macos.sh arm64
MACOSX_DEPLOYMENT_TARGET=14.0 ./bin/prepare-macos.sh x86_64
```

Build and install caches are separated by architecture and minimum macOS version:
for example, `build/macos-arm64-14.0` and `local-static/macos-arm64-14.0`.
The older directories without a version suffix are not reused. The wrapper also
honors `TTREK_BUILD_DIR` and `TTREK_INSTALL_DIR`; use fresh directories for both
when changing the SDK or repairing an existing cache. Keep custom directories
separate for each architecture and deployment target.

After collecting both tested binaries in `dist/` on a Mac, optionally combine
and verify them with `lipo`:

```sh
ttrek run -script prepare-macos-universal2
# Equivalent: ./bin/prepare-universal2.sh
```

This writes `dist/macos-universal2/ttrek`. The two inputs may come from different
Macs. macOS uses static third-party dependencies and dynamic system frameworks.
Use the same SDK and minimum `MACOSX_DEPLOYMENT_TARGET` for both builds, and test
each architecture on the macOS versions you support.

## Upload release binaries and record checksums

Uploads are separate from preparation. Collect the desired `dist/<target>/ttrek`
files on one machine, then supply `local` or `prod`, an explicit stable version,
and optionally target names. Without target names, all prepared binaries upload.
Use the version compiled into those binaries (`CMakeLists.txt`).

```sh
ttrek run -script upload-local -- 1.0.0 linux-amd64-glibc
ttrek run -script upload-prod -- 1.0.0
# Equivalent direct invocation:
./bin/upload.sh local 1.0.0 linux-amd64-glibc
```

Local uploads use curl PUTs to the running mock S3 server at
`http://127.0.0.1:3015`, creating the `ttrek-local` bucket if needed.
They also copy the binaries into `../ttrek.sh/dist/s3` so the local stack retains
them when it restarts with a fresh mock database.
Production uploads use the AWS CLI, profile `ttrek-prod-neo`, region
`eu-central-1`, and the existing private bucket `jerily-ttrek-app`.
Objects use `ttrek/<version>/<target>/ttrek`.

Python 3 computes SHA-256 hashes. After all selected uploads succeed, the script
atomically updates `../ttrek.sh/etc/releases.json`, preserving other versions,
targets, and environments. The JSON maps `local`/`prod` → version → target → hash;
the website's `[releases] checksums_file` references it. Production refuses to
replace a recorded version/target with different bytes; use a new version.
Local builds can replace their own checksums. Run uploads sequentially against
the shared file, and keep it under version control in the website repository.

Overrides: `TTREK_CHECKSUMS_FILE` (manifest path, whose parent must exist),
`TTREK_LOCAL_S3_ENDPOINT`, `TTREK_LOCAL_S3_DIR` (the stack's `--s3-dir` seed directory),
`TTREK_S3_BUCKET`, `TTREK_S3_PREFIX`, `AWS_PROFILE`,
and `AWS_REGION`. Relative paths resolve from this repository. Set
`TTREK_CHECKSUMS_FILE` when the website checkout is elsewhere.

Update the website's `releases.version` and `releases.prefix` together, deploy
the JSON with its configuration, and restart the website/local stack after
uploading. Object uploads do not deploy the website or change its configuration.

Run `python3 tests/releases.py` to check orchestration and checksum history with
mocked Docker/AWS commands and a temporary local HTTP server.
