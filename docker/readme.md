# Docker builds

For release artifacts, use `Dockerfile.release` from the repository root.
It accepts `BASE_IMAGE=ubuntu:22.04` (glibc, the default) or `alpine:3.23`
(musl), and `--platform linux/amd64` or `linux/arm64`. `JOBS` defaults to 2.
It reuses `build-ttrek.sh`, including the solver and ttrek tests, and exports
only `ttrek` through Docker's local exporter.

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

`Dockerfile.alpine` remains available as a development image with build tools,
sources and tests. It uses Alpine 3.23 and the script's two-job default:

```sh
docker build --platform linux/amd64 -f docker/Dockerfile.alpine -t ttrek-alpine:dev .
docker run --rm -it ttrek-alpine:dev bash
```
