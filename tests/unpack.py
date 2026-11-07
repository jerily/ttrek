"""Keep shipped generated files newer than their inputs when unpacking sources."""
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import zipfile

BINARY = Path(sys.argv[1]).resolve()

with tempfile.TemporaryDirectory(prefix="ttrek unpack ") as temporary:
    # macOS temporary directories can start with /var, a symlink to /private/var.
    root = Path(temporary).resolve()
    source = root / "source"
    source.mkdir()
    # Extract the generated parser before its older input, as PostgreSQL's
    # release archive does. Using extraction times would force regeneration.
    files = {
        "gram.c": ("/* shipped parser */\n", 1700000010),
        "gram.y": ("/* grammar */\n", 1700000000),
        "Makefile": ("all: gram.c\ngram.c: gram.y\n\t@exit 1\n", 1700000000),
    }
    for name, (contents, mtime) in files.items():
        path = source / name
        path.write_text(contents)
        os.utime(path, (mtime, mtime))

    for extension in ("tar.gz", "zip"):
        archive = root / ("source." + extension)
        if extension == "tar.gz":
            with tarfile.open(archive, "w:gz") as output:
                for name in files:
                    output.add(source / name, arcname="source/" + name)
        else:
            with zipfile.ZipFile(archive, "w") as output:
                for name in files:
                    output.write(source / name, arcname="source/" + name)

        destination = root / ("unpacked " + extension)
        destination.mkdir()
        result = subprocess.run(
            [BINARY, "unpack", archive, destination], cwd=root,
            env=dict(os.environ, TTREK_NO_UPDATE_CHECK="1"),
            capture_output=True, text=True, timeout=30)
        assert result.returncode == 0, (result.stdout, result.stderr)
        # An up-to-date shipped parser must not require Bison to be installed.
        result = subprocess.run(["make", "-q", "all"], cwd=destination,
                                capture_output=True, text=True, timeout=30)
        assert result.returncode == 0, (extension, result.stdout, result.stderr)
        for name, (contents, mtime) in files.items():
            path = destination / name
            assert path.read_text() == contents, (extension, name)
            assert path.stat().st_mtime == mtime, (extension, name, path.stat().st_mtime)
