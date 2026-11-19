#!/bin/sh
set -eu

cd "$(dirname "$0")/.."
root=$(pwd -P)
version=$(dpkg-parsechangelog -S Version)
upstream=${version%-*}
archive="ttrek_${upstream}.orig.tar.gz"

"$root/bin/prepare-source.py" "v$upstream"
mkdir -p "build/ppa-v$upstream"
output=$(mktemp -d "$root/build/ppa-v$upstream/$version.XXXXXX")
cp "dist/ttrek-$upstream.tar.gz" "$output/$archive"

# Use the current Ubuntu packaging with the tagged upstream sources.
tar -xzf "$output/$archive" -C "$output" --exclude="ttrek-$upstream/debian"
cp -R debian "$output/ttrek-$upstream/debian"
cd "$output/ttrek-$upstream"
dpkg-buildpackage -S -si -nc -d -us -uc
printf '\nUnsigned PPA upload: %s/ttrek_%s_source.changes\n' "$output" "$version"
