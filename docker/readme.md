# Docker builds

For release artifacts, use `Dockerfile.release` from the repository root.
Use `bin/prepare-linux.sh` to select the pinned manylinux_2_28 image for glibc
or Alpine 3.23 for musl, with `--platform linux/amd64` or `linux/arm64`.
The wrapper also passes `LIBC=glibc|musl`. `JOBS` defaults to 2.
It reuses `build-ttrek.sh`, including the solver and ttrek tests, and exports
only `ttrek` through Docker's local exporter after checking its ELF ABI.
Glibc releases require at most glibc 2.28; musl releases remain fully static.

The release scripts in `bin/` wrap these builds. For example, build Alpine/musl
for ARM64 from the repository root:

```sh
ttrek run -script prepare-linux-arm64-musl
# Or: ./bin/prepare-linux.sh arm64 musl
```

See the [build documentation](../readme.md#linux-release-binaries) for all four
Linux variants, macOS builds, architecture isolation, universal2 packaging,
and local/production uploads with versioned checksums.
Foreign architectures require a native builder or emulation; Docker Desktop
provides emulation.

After preparing both libc variants, run `./bin/test-linux-release.sh amd64`
or `arm64` to test them in AlmaLinux 8, AL2023, and Ubuntu 24.04. The optional
second argument selects `alma8`, `al2023`, or `ubuntu`. `Dockerfile.runtime-test`
pins the base images and reruns offline verification against the exported
binaries, including HTTPS trust-store checks and package builds. See
[release validation](../docs/releases.md#linux-release-binaries) for details.

`Dockerfile.alpine` remains available as a development image with build tools,
sources and tests. It uses Alpine 3.23 and the script's two-job default:

```sh
docker build --platform linux/amd64 -f docker/Dockerfile.alpine -t ttrek-alpine:dev .
docker run --rm -it ttrek-alpine:dev bash
```
