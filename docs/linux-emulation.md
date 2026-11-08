# Build Linux releases through emulation

[Release guide](releases.md)

The release scripts build and test inside Docker. The container architecture,
not the host operating system, determines which compiler and libraries run.

| Host CPU | Native Linux targets | Emulated Linux targets |
| --- | --- | --- |
| x86_64 / Intel | amd64 glibc, amd64 musl | arm64 glibc, arm64 musl |
| ARM64 / Apple Silicon | arm64 glibc, arm64 musl | amd64 glibc, amd64 musl |

Docker Desktop on a Mac supplies emulation. Install Docker Desktop and its
Buildx plugin, then confirm the desired platform appears in
`docker buildx inspect --bootstrap`. Apple Silicon's Intel macOS builds use
Rosetta separately; Rosetta is not the Linux build mechanism described here.

## Linux host setup

On an x86_64 Linux host, inspect the existing binfmt registrations:

```sh
binfmt_image=tonistiigi/binfmt@sha256:400a4873b838d1b89194d982c45e5fb3cda4593fbfd7e08a02e76b03b21166f0
docker run --privileged --rm "$binfmt_image"
```

Only if `linux/arm64` is missing, register that emulator:

```sh
docker run --privileged --rm "$binfmt_image" --install arm64
```

For an ARM64 Linux host building amd64, use `--install amd64` instead.
Register emulation before starting the builder. A builder that was already
running needs to be stopped and bootstrapped again to see new registrations.

## Build and test

Run these commands from the ttrek checkout. Use an isolated builder without
changing Docker's default:

```sh
docker buildx create --name ttrek-emulated --driver docker-container
docker buildx inspect ttrek-emulated --bootstrap
export BUILDX_BUILDER=ttrek-emulated
export JOBS=2

# Emulated ARM64 on an x86_64 host:
./bin/prepare-linux.sh arm64 glibc
./bin/prepare-linux.sh arm64 musl
./bin/test-linux-release.sh arm64
```

For emulated amd64 on Apple Silicon or another ARM64 host, replace `arm64`
with `amd64` in the three commands. The test script checks both libc binaries
on AlmaLinux 8, Amazon Linux 2023, and Ubuntu 24.04. Build steps include the
CLI tests and ELF ABI checks; runtime tests run without external networking.

Outputs are `dist/linux-<architecture>-glibc/ttrek` and
`dist/linux-<architecture>-musl/ttrek`. Dependencies remain in the builder's
architecture-specific cache. Emulated compilation is slower; retaining this
builder avoids rebuilding unchanged dependencies on later runs.

## Cleanup

Stop the builder to retain its cache, or remove it when the cache is no longer
needed:

```sh
docker buildx stop ttrek-emulated
# Optional: docker buildx rm ttrek-emulated
```

On Linux, remove a binfmt registration only if you added it for this work and
nothing else needs it:

```sh
docker run --privileged --rm "$binfmt_image" --uninstall qemu-aarch64
# For an amd64 registration: --uninstall qemu-x86_64
```
