#!/bin/sh
set -eu

if [ "$#" -gt 1 ]; then
    echo "Usage: $0 [prepared-source-directory]" >&2
    exit 2
fi
root=$(cd "$(dirname "$0")/.." && pwd -P)
src=$(cd "${1:-.}" && pwd -P)
cd "$src"
version=$(dpkg-parsechangelog -S Version)
if [ "$(dpkg-parsechangelog -S Source)" != ttrek ] ||
   [ "$(dpkg-parsechangelog -S Distribution)" != unstable ]; then
    echo "Expected a prepared ttrek source tree with a changelog targeting unstable." >&2
    exit 1
fi
upstream=${version%-*}
archive="ttrek_${upstream}.orig.tar.gz"
if [ ! -f "../$archive" ]; then
    echo "Missing upstream source archive: $src/../$archive" >&2
    exit 1
fi

mkdir -p "$root/build/debian-v$upstream"
output=$(mktemp -d "$root/build/debian-v$upstream/$version.XXXXXX")
printf 'Build output: %s\n' "$output"

docker run --rm -i \
    -v "$src:/input:ro" \
    -v "$src/../$archive:/build/$archive:ro" \
    -v "$output:/out" \
    -e DEBIAN_FRONTEND=noninteractive \
    debian:sid sh -se -- "$version" "$upstream" "$(id -u):$(id -g)" <<'BUILD'
version=$1
upstream=$2
owner=$3
cd /build
cp -a /input "ttrek-$upstream"
cd "ttrek-$upstream"
apt-get update
apt-get install -y --no-install-recommends build-essential dpkg-dev
apt-get build-dep -y .
dpkg-buildpackage -S -sa -us -uc

cd /build
dpkg-source -x "ttrek_${version}.dsc" rebuild
cd rebuild
dpkg-buildpackage -b -us -uc

cd /build
arch=$(dpkg --print-architecture)
apt-get install -y "./ttrek_${version}_${arch}.deb"
ttrek version

# Include every file from both builds, including any debug symbol package.
find . -maxdepth 1 -type f -exec cp -t /out -- {} +
chown "$owner" /out/*
apt-get install -y --no-install-recommends lintian
lintian "ttrek_${version}_source.changes" "ttrek_${version}_${arch}.changes"
BUILD

printf '\nUnsigned Debian source upload: %s/ttrek_%s_source.changes\n' "$output" "$version"
