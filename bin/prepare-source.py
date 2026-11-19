#!/usr/bin/env python3
"""Bundle a ttrek revision with the solver revision recorded in its Git tree."""
import argparse
import gzip
import io
import json
from pathlib import Path
import re
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOLVER = "vendor/satdepsolver-cpp"


def git(*args, repo=ROOT):
    return subprocess.check_output(["git", "-C", str(repo), *args])


def append_tree(output, repo, revision, prefix, skip=None):
    data = git("archive", "--format=tar", "--prefix=" + prefix, revision, repo=repo)
    with tarfile.open(fileobj=io.BytesIO(data)) as archive:
        for entry in archive:
            if entry.name == skip:
                continue
            output.addfile(entry, archive.extractfile(entry) if entry.isfile() else None)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("revision", help="release tag or Git revision")
    args = parser.parse_args()
    revision = git("rev-parse", "--verify", "--end-of-options", args.revision + "^{}").decode().strip()
    version = json.loads(git("show", revision + ":ttrek.json"))["version"]
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version):
        parser.error("expected a stable version in ttrek.json")
    link = git("ls-tree", revision, "--", SOLVER).split()
    if link[:2] != [b"160000", b"commit"]:
        parser.error(f"{revision} does not record a submodule at {SOLVER}")

    prefix = f"ttrek-{version}/"
    dist = ROOT / "dist"
    dist.mkdir(exist_ok=True)
    target = dist / f"ttrek-{version}.tar.gz"
    with tempfile.TemporaryDirectory(dir=dist) as tmp:
        staged = Path(tmp) / target.name
        with staged.open("wb") as raw, gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0) as gz:
            with tarfile.open(mode="w|", fileobj=gz) as archive:
                append_tree(archive, ROOT, revision, prefix, skip=prefix + SOLVER)
                append_tree(archive, ROOT / SOLVER, link[2].decode(), prefix + SOLVER + "/")
        staged.replace(target)
    print(target)


if __name__ == "__main__":
    main()
