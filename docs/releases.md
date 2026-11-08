# Prepare and publish releases

[Documentation](readme.md) · [Project overview](../readme.md)

These instructions are for release maintainers. To install ttrek as a user,
see [Install ttrek](install.md). Run the commands below from the repository root;
relative paths refer to that directory.

## Linux release binaries

`bin/prepare-linux.sh` uses Docker Buildx and the existing build/tests in
`docker/Dockerfile.release`. Run each target on a native builder or one with
emulation for the requested architecture. Glibc builds use the architecture's
digest-pinned [manylinux_2_28 image](https://github.com/pypa/manylinux#manylinux_2_28-almalinux-8-based)
(AlmaLinux 8 with GCC 14); Alpine 3.23 selects musl. Each script exports
`dist/<target>/ttrek`:

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
Musl builds are fully static. New glibc builds target glibc 2.28 or newer,
with static third-party and C++ runtime libraries. Previously built Ubuntu
22.04 artifacts require glibc 2.35 and must be rebuilt. Dynamic glibc keeps
hostname lookups compatible with the host's NSS modules. The installer retains libc-aware selection:
glibc on glibc systems, musl on musl systems. Self-update retains the binary's
libc target.

The entire dependency build runs inside the pinned image, using its modern
compiler, CMake (at least 3.22.1), and Python 3. No host build/install cache is
copied in, and no `-march=native` flags are used. When updating the image pins
in `bin/prepare-linux.sh`, update the AMD64 default in `Dockerfile.release`
too and repeat runtime validation on both architectures.

Before export, `bin/check-linux-release.py` checks the ELF architecture,
loader, symbol versions, and dynamic dependencies. It rejects glibc symbols
newer than 2.28, non-system shared libraries (including dynamic libstdc++),
and embedded library search paths. Musl outputs must have no dynamic loader
or shared-library dependencies. The checker can also inspect collected files:

```sh
python3 bin/check-linux-release.py dist/linux-amd64-glibc/ttrek amd64 glibc
python3 bin/check-linux-release.py dist/linux-arm64-glibc/ttrek arm64 glibc
python3 tests/release_abi.py
```

Before publishing, run the checked-in runtime matrix on AlmaLinux 8,
Amazon Linux 2023, and Ubuntu 24.04. Prepare both libc binaries for the
selected architecture first:

```sh
./bin/test-linux-release.sh amd64
./bin/test-linux-release.sh arm64
# Run one distribution while investigating a failure:
./bin/test-linux-release.sh amd64 al2023
# Equivalent ttrek aliases:
ttrek run -script test-linux-amd64
ttrek run -script test-linux-arm64 -- ubuntu
```

`docker/Dockerfile.runtime-test` uses digest-pinned runtime images, their own
compilers and certificate stores, and a pinned CMake package on the RPM-based
images. Provisioning needs network access to distribution repositories/PyPI;
the verification stage runs with networking disabled and is rerun on every
invocation. OS packages still come from the distributions' current repositories.
No AWS account, public registry, or running website is required.

Each run tests both glibc and musl artifacts: version/startup, ELF ABI,
hostname lookup, HTTPS downloads and registry access, CA overrides, rejection
of untrusted certificates and hostname mismatches, and package builds/updates
through the local registry fixture. A temporary test CA is installed and
removed inside each disposable container to verify the runtime's default
trust store. Do not run `tests/https.py --system-trust` on a host machine.

See [Linux builds through emulation](linux-emulation.md) for host/target
combinations and the complete build/test workflow.

Foreign architectures require a native builder or emulation. On a Linux
AMD64 host, register ARM64 with the kernel before starting the builder.
BuildKit's bundled emulator alone can fail when package scripts execute
through their shebang or the shell's fallback. Docker Desktop already
provides this registration. Following [Docker's QEMU setup](https://docs.docker.com/build/building/multi-platform/#qemu),
check the existing registrations and install ARM64 only if it is missing:

```sh
binfmt_image=tonistiigi/binfmt@sha256:400a4873b838d1b89194d982c45e5fb3cda4593fbfd7e08a02e76b03b21166f0
docker run --privileged --rm "$binfmt_image"
# Only when linux/arm64 is not already supported:
docker run --privileged --rm "$binfmt_image" --install arm64

docker buildx create --name ttrek-release-tests --driver docker-container
docker buildx inspect ttrek-release-tests --bootstrap
# Its Platforms list should include linux/arm64.
BUILDX_BUILDER=ttrek-release-tests ./bin/prepare-linux.sh arm64 glibc
BUILDX_BUILDER=ttrek-release-tests ./bin/prepare-linux.sh arm64 musl
BUILDX_BUILDER=ttrek-release-tests ./bin/test-linux-release.sh arm64
docker buildx rm ttrek-release-tests
# Only if this session added qemu-aarch64:
docker run --privileged --rm "$binfmt_image" --uninstall qemu-aarch64
```

This selects an isolated builder without changing the default builder. If
registration was added after a builder started, stop it with
`docker buildx stop <name>` and bootstrap it again before retrying; its build
cache is retained.

The ABI check complements these runtime tests; it does not replace them.

Linux binaries select the runtime distribution's CA bundle for HTTPS instead
of relying on the builder's certificate path. `CURL_CA_BUNDLE` or
`SSL_CERT_FILE` can select an explicit bundle, with `CURL_CA_BUNDLE` taking
precedence. An invalid explicit path fails verification rather than falling
back to a different trust store.

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

To build every architecture this Mac can execute with minimum macOS 11.0:

```sh
./bin/prepare-macos-target-11.0.sh
# Or: ttrek run -script prepare-macos-target-11.0
```

Apple Silicon builds ARM64 and x86_64 (with Rosetta and both toolsets), then
combines them into universal2 without rebuilding either input. Intel Macs build
x86_64. This wrapper always sets `MACOSX_DEPLOYMENT_TARGET=11.0`.

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

The baseline rebuild keeps version 1.0.0. After resetting the website's
checksum manifest, repopulate it by uploading the tested artifacts for all
advertised targets before deploying the manifest. Once the rebuilt AMD64
glibc object is uploaded and tested on AL2023, CodeBuild can switch to
`s3://jerily-ttrek-app/ttrek/1.0.0/linux-amd64-glibc/ttrek` in
`ttrek.sh/devops/build-lambda.sh`. Its existing `ttrek/*` read permission
already covers that object.

Run `python3 tests/releases.py` to check orchestration and checksum history with
mocked Docker/AWS commands and a temporary local HTTP server.
