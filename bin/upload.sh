#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 || ! $1 =~ ^(local|prod)$ || ! $2 =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "Usage: $0 local|prod VERSION [TARGET ...]" >&2
    echo "Uploads dist/<target>/ttrek; defaults to all prepared targets." >&2
    exit 1
fi
environment=$1
version=$2
shift 2
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."

targets=("$@")
if [[ ${#targets[@]} == 0 ]]; then
    shopt -s nullglob
    for binary in dist/*/ttrek; do
        target=${binary#dist/}
        targets+=("${target%/ttrek}")
    done
fi
if [[ ${#targets[@]} == 0 ]]; then
    echo "No release binaries found in dist/. Run a prepare script first." >&2
    exit 1
fi

# Validate every input before starting any upload.
for target in "${targets[@]}"; do
    case $target in
        linux-amd64-glibc|linux-arm64-glibc|linux-amd64-musl|linux-arm64-musl|macos-arm64|macos-x86_64|macos-universal2) ;;
        *) echo "Unknown release target: $target" >&2; exit 1 ;;
    esac
    binary=dist/$target/ttrek
    if [[ ! -s $binary ]]; then
        echo "Missing or empty release binary: $binary" >&2
        exit 1
    fi
done

# Keep local builds separate from production, with history for every version.
# Override this path when the website is not in the sibling ttrek.sh checkout.
manifest=${TTREK_CHECKSUMS_FILE:-"$PWD/../ttrek.sh/etc/releases.json"}
pending=$(mktemp "$manifest.XXXXXX")
trap 'rm -f "$pending"' EXIT
python3 - "$manifest" "$pending" "$environment" "$version" "${targets[@]}" <<'PY'
import hashlib
import json
from pathlib import Path
import sys

manifest, pending = map(Path, sys.argv[1:3])
environment, version = sys.argv[3:5]
releases = json.loads(manifest.read_text()) if manifest.exists() else {}
checksums = releases.setdefault(environment, {}).setdefault(version, {})
for target in sys.argv[5:]:
    digest = hashlib.sha256()
    with open(f"dist/{target}/ttrek", "rb") as binary:
        for block in iter(lambda: binary.read(1024 * 1024), b""):
            digest.update(block)
    checksum = digest.hexdigest()
    if environment == "prod" and target in checksums and checksums[target].lower() != checksum:
        raise SystemExit(f"Production {version}/{target} already has a different checksum; use a new version.")
    checksums[target] = checksum
pending.write_text(json.dumps(releases, indent=2, sort_keys=True) + "\n")
PY

prefix=${TTREK_S3_PREFIX:-ttrek/$version}
prefix=${prefix#/}
prefix=${prefix%/}
case $environment in
    local)
        bucket=${TTREK_S3_BUCKET:-ttrek-local}
        endpoint=${TTREK_LOCAL_S3_ENDPOINT:-http://127.0.0.1:3015}
        endpoint=${endpoint%/}
        region=us-east-1
        # mock-s3-app accepts ordinary PUTs, including bucket creation.
        curl --fail --silent --show-error --output /dev/null --request PUT "$endpoint/$bucket"
        ;;
    prod)
        bucket=${TTREK_S3_BUCKET:-jerily-ttrek-app}
        region=${AWS_REGION:-eu-central-1}
        ;;
esac

for target in "${targets[@]}"; do
    binary=dist/$target/ttrek
    key=$prefix/$target/ttrek
    echo "Uploading $binary to $environment: s3://$bucket/$key" >&2
    if [[ $environment == local ]]; then
        curl --fail --silent --show-error --output /dev/null \
            --header 'Content-Type: application/octet-stream' \
            --upload-file "$binary" "$endpoint/$bucket/$key"
        # The local stack discards its object database on shutdown. Keep the
        # uploaded bytes in its seed directory so a restart preserves them.
        seed=${TTREK_LOCAL_S3_DIR:-"$PWD/../ttrek.sh/dist/s3"}/$bucket/$key
        mkdir -p "$(dirname -- "$seed")"
        cp "$binary" "$seed"
    else
        aws --profile "${AWS_PROFILE:-ttrek-prod-neo}" --region "$region" \
            s3 cp "$binary" "s3://$bucket/$key" \
            --content-type application/octet-stream --only-show-errors
    fi
done

# Publish the checksums only after every selected object was uploaded.
chmod 644 "$pending"
mv "$pending" "$manifest"
trap - EXIT
printf 'Updated %s (%s, version %s).\n' "$manifest" "$environment" "$version"
printf 'Set releases.version=%s and releases.prefix=%s in the website config.\n' "$version" "$prefix"
