"""Autoconf is required only by enabled build steps, before source downloads."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile

binary, cmake, curl = (Path(arg).absolute() for arg in sys.argv[1:])
with tempfile.TemporaryDirectory(prefix="ttrek-recipe-dependencies-") as temporary:
    root = Path(temporary)
    tools = root / "bin"
    tools.mkdir()
    for name in ("bash", "sh", "make", "git", "patch", "pkg-config", "perl", "tar",
                 "gzip", "cc", "c++", "uname", "rm", "mkdir", "nproc", "dirname",
                 "cat", "cp", "mv", "mktemp", "base64", "unzip"):
        path = shutil.which(name)
        if path:
            (tools / name).symlink_to(path)
    (tools / "cmake").symlink_to(cmake)
    (tools / "curl").symlink_to(curl)
    env = dict(os.environ, PATH=str(tools), TTREK_NO_UPDATE_CHECK="1", IS_TTY="0")
    env.pop("CC", None)
    env.pop("CXX", None)
    source = root / "source"
    source.mkdir()
    bootstrap = source / "bootstrap.sh"
    bootstrap.write_text('#!/bin/sh\nprintf built > "$1/marker"\n')
    bootstrap.chmod(0o755)
    archive = root / "source.tar.gz"
    with tarfile.open(archive, "w:gz") as output:
        output.add(source, arcname="source")
    registry = root / "registry"
    recipe_dir = registry / "example/1.0.0"
    recipe_dir.mkdir(parents=True)
    steps = [{"cmd": "download", "url": archive.as_uri()}, {"cmd": "unpack"},
             {"cmd": "autogen", "path": "autoconf"},
             {"cmd": "autogen", "path": "./bootstrap.sh",
              "options": [{"name": "$INSTALL_DIR"}]}]

    def recipe(autoconf):
        selected = list(steps)
        if autoconf == "disabled":
            selected[2] = dict(steps[2], **{"if": "+static"})
        elif not autoconf:
            del selected[2]
        (recipe_dir / "ttrek.json").write_text(json.dumps({
            "dependencies": {}, "build": {"default": selected}}))

    def project(name):
        directory = root / name
        directory.mkdir()
        (directory / "ttrek.json").write_text(json.dumps({"dependencies": {"example": "1.0.0"}}))
        return directory

    def run(directory, *args):
        return subprocess.run([binary, *args, "-registry", str(registry)], cwd=directory,
                              env=env, capture_output=True, text=True, timeout=60)

    def missing(result):
        assert result.returncode != 0, result
        for text in ("example@1.0.0 requires autoconf", "brew install autoconf",
                     "sudo apt install autoconf"):
            assert text in result.stdout + result.stderr, result

    recipe(True)
    for command in ("install", "update"):
        directory = project(command)
        missing(run(directory, command, "-y"))
        assert not list((directory / "ttrek-venv/build").rglob("download.log"))
        assert not (directory / "ttrek-venv/local/marker").exists()

    # Generation is allowed without Autoconf; execution checks the target host.
    directory = project("scripts")
    for shell in ("bash", "sh"):
        generated = run(directory, "gen-script", "-shell", shell)
        assert generated.returncode == 0, generated
        script = directory / (shell + ".sh")
        script.write_text(generated.stdout)
        missing(subprocess.run([str(tools / shell), str(script)], cwd=directory,
                               env=env, capture_output=True, text=True, timeout=30))
        assert not list((directory / "build").rglob("download.log"))

    for condition in (False, "disabled"):
        recipe(condition)
        directory = project(str(condition))
        result = run(directory, "update", "-y")
        assert result.returncode == 0, result
        assert (directory / "ttrek-venv/local/marker").read_text() == "built"

    recipe(True)
    autoconf = tools / "autoconf"
    autoconf.write_text("#!/bin/sh\nexit 0\n")
    autoconf.chmod(0o755)
    directory = project("available")
    result = run(directory, "install", "-y")
    assert result.returncode == 0, result
    autoconf.unlink()
    result = run(directory, "update", "-y")
    assert result.returncode == 0 and "Nothing to install!" in result.stdout, result

print("Recipe-specific Autoconf checks passed for install, update, bash and sh")
