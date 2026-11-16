"""Check the whole recipe plan before builds, including host files and tools."""
import copy
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tarfile
import tempfile

binary, cmake, curl = (Path(arg).absolute() for arg in sys.argv[1:])
with tempfile.TemporaryDirectory(prefix="ttrek-external-dependencies-") as temporary:
    root = Path(temporary)
    tools = root / "bin"
    tools.mkdir()
    for name in ("bash", "sh", "make", "git", "patch", "pkg-config", "perl", "tar", "gzip",
                 "cc", "c++", "uname", "rm", "mkdir", "nproc", "sysctl", "xcrun", "dirname",
                 "cat", "cp", "mv", "mktemp", "base64", "unzip", "find"):
        path = shutil.which(name)
        if path:
            (tools / name).symlink_to(path)
    (tools / "curl").symlink_to(curl)
    # Deliberately omit both CMake and Autoconf.
    env = dict(os.environ, PATH=str(tools), HOME=str(root), TTREK_NO_UPDATE_CHECK="1", IS_TTY="0")
    for key in ("CC", "CXX", "TTREK_DEBUG"):
        env.pop(key, None)
    registry = root / "registry"
    source = root / "source"
    source.mkdir()
    bootstrap = source / "bootstrap.sh"
    bootstrap.write_text('#!/bin/sh\nprintf built > "$1/$2-marker"\n')
    bootstrap.chmod(0o755)
    archive = root / "source.tar.gz"
    with tarfile.open(archive, "w:gz") as output:
        output.add(source, arcname="source")

    def build_steps(name):
        return [{"cmd": "download", "url": archive.as_uri()}, {"cmd": "unpack"},
                {"cmd": "autogen", "path": "./bootstrap.sh",
                 "options": [{"name": "$INSTALL_DIR"}, {"name": name}]}]

    def recipe(name, dependencies=None, external=None, steps=None, version="1.0.0", build=None):
        directory = registry / name / version
        directory.mkdir(parents=True, exist_ok=True)
        spec = {"dependencies": dependencies or {},
                "build": build or {"default": build_steps(name) if steps is None else steps}}
        if external is not None:
            spec["externalDependencies"] = external
        (directory / "ttrek.json").write_text(json.dumps(spec))

    def project(label, packages=None, **extra):
        directory = root / label
        directory.mkdir()
        (directory / "ttrek.json").write_text(json.dumps({
            "dependencies": packages if packages is not None else {"top": "1.0.0"}, **extra}))
        return directory

    def run(directory, *args, ok=True):
        result = subprocess.run([binary, *args, "-registry", str(registry)], cwd=directory,
                                env=env, capture_output=True, text=True, timeout=60)
        assert (result.returncode == 0) == ok, (args, result)
        return result

    def untouched(directory):
        assert not list((directory / "ttrek-venv/build").rglob("download.log"))
        assert not list((directory / "ttrek-venv/local").glob("*-marker"))

    # Include shell syntax and quotes: all file paths, hints and URLs are data.
    first = root / "header '$(touch injected)`.h"
    second = root / "library file.so"
    hint = "install host-sdk; $(touch hint-ran) `touch hint-ran` 'quoted'"
    external = {"host-sdk": {"platforms": [platform.system().lower()],
                             "check": {"files": [str(first), str(second)]},
                             "installHint": hint, "url": "https://example.invalid/sdk"}}
    recipe("early")
    recipe("middle", {"early": "1.0.0"}, external)
    recipe("top", {"middle": "^1.0.0"})
    for command in ("install", "update"):
        directory = project(command)
        original = (directory / "ttrek.json").read_bytes()
        failed = run(directory, command, "-y", ok=False)
        for message in ("middle@1.0.0 requires host-sdk", str(first), str(second), hint,
                        "https://example.invalid/sdk"):
            assert message in failed.stderr, failed
        untouched(directory)
        assert (directory / "ttrek.json").read_bytes() == original
        assert not (directory / "hint-ran").exists() and not (directory / "injected").exists()

    # Generation stays read-only, and emitted checks run before every package.
    directory = project("scripts")
    run(directory, "pretend")
    for shell in ("bash", "sh"):
        generated = run(directory, "gen-script", "-shell", shell)
        assert not (directory / "ttrek-venv").exists()
        script = directory / (shell + ".sh")
        script.write_text(generated.stdout)
        failed = subprocess.run([tools / shell, script], cwd=directory, env=env,
                                capture_output=True, text=True, timeout=30)
        assert failed.returncode != 0 and hint in failed.stderr, failed
        assert not list((directory / "build").rglob("download.log"))
        assert not list((directory / "build/install").glob("*-marker"))
        assert not (directory / "hint-ran").exists() and not (directory / "injected").exists()

    first.touch()
    directory = project("one-file")
    failed = run(directory, "install", "-y", ok=False)
    assert str(second) in failed.stderr and str(first) not in failed.stderr, failed
    second.mkdir()  # A directory cannot satisfy a required file.
    failed = run(directory, "install", "-y", ok=False)
    assert str(second) in failed.stderr, failed
    second.rmdir()
    second.symlink_to(first)
    directory = project("available")
    result = run(directory, "install", "-y")
    assert hint not in result.stderr
    for name in ("early", "middle", "top"):
        assert (directory / f"ttrek-venv/local/{name}-marker").read_text() == "built"
    first.unlink()
    run(directory, "install", "-y")  # No rebuilding: no host build requirements.
    failed = run(directory, "install", "-y", "-force", "middle", ok=False)
    assert hint in failed.stderr
    for name in ("early", "middle", "top"):
        assert (directory / f"ttrek-venv/local/{name}-marker").read_text() == "built"

    # Updates fail before replacing old files, even when an earlier package changes.
    recipe("early", version="1.1.0")
    recipe("middle", {"early": "1.1.0"}, external, version="1.1.0")
    before = {path: path.read_bytes() for path in (
        directory / "ttrek-lock.json", directory / "ttrek-venv/ttrek-manifest.json")}
    assert hint in run(directory, "update", "-y", "middle@1.1.0", ok=False).stderr
    assert all(path.read_bytes() == content for path, content in before.items())
    assert (directory / "ttrek-venv/local/early-marker").read_text() == "built"

    recipe("top", {"middle": "1.0.0"})
    for platforms in (["not-this-os"], []):
        skipped = copy.deepcopy(external)
        skipped["host-sdk"]["platforms"] = platforms
        recipe("middle", {"early": "1.0.0"}, skipped)
        run(project("skip-" + str(len(platforms))), "install", "-y")
    universal = copy.deepcopy(external)
    del universal["host-sdk"]["platforms"]
    recipe("middle", {"early": "1.0.0"}, universal)
    assert hint in run(project("all-platforms"), "install", "-y", ok=False).stderr

    # Reject invalid metadata instead of silently skipping requirements.
    for index, invalid in enumerate(([], {"sdk": "invalid"}, {"sdk": {"check": {"files": []}}},
                                     {"sdk": {"check": {"files": [1]}}},
                                     {"sdk": {"check": {"files": [str(first)]}, "installHint": []}})):
        recipe("middle", {"early": "1.0.0"}, invalid)
        directory = project(f"invalid-{index}")
        assert "externalDependencies" in run(directory, "install", "-y", ok=False).stderr
        untouched(directory)

    # Both missing build tools are reported together, before early can build.
    recipe("middle", {"early": "1.0.0"}, steps=[{"cmd": "autogen", "path": "autoconf"}])
    recipe("top", {"middle": "1.0.0"}, steps=[{"cmd": "cmake_config"}])
    directory = project("tools-missing")
    failed = run(directory, "install", "-y", ok=False)
    for text in ("middle@1.0.0 requires autoconf", "top@1.0.0 requires cmake",
                 "brew install cmake", "sudo apt install cmake"):
        assert text in failed.stderr, failed
    untouched(directory)

    for shell in ("bash", "sh"):
        generated = run(directory, "gen-script", "-shell", shell)
        script = directory / (shell + ".sh")
        script.write_text(generated.stdout)
        failed = subprocess.run([tools / shell, script], cwd=directory, env=env,
                                capture_output=True, text=True, timeout=30)
        assert failed.returncode != 0, failed
        assert "requires cmake" in failed.stderr and "requires autoconf" in failed.stderr, failed
        assert not (directory / "build/install/early-marker").exists()

    recipe("middle", {"early": "1.0.0"})
    for command in ("cmake_config", "cmake_make", "cmake_install"):
        recipe("top", {"middle": "1.0.0"}, steps=[{"cmd": command, "if": "+static"}])
        run(project(command + "-disabled"), "install", "-y")
        directory = project(command + "-enabled", useFlags=["+static"])
        assert "requires cmake" in run(directory, "install", "-y", ok=False).stderr
        untouched(directory)
    # The registry's selected platform build is what gets checked.
    target = platform.system().lower() + "." + platform.machine().lower()
    recipe("top", build={"default": [{"cmd": "cmake_config"}], target: []})
    run(project("platform-build"), "install", "-y")

    # Project-local builds are checked before their package dependencies build.
    directory = project("project-build", {"early": "1.0.0"}, name="project", version="1.0.0",
                        build={"default": [{"cmd": "cmake_config"}]})
    assert "project@1.0.0 requires cmake" in run(directory, "install", "-y", ok=False).stderr
    untouched(directory)

    directory = project("project-files", {"early": "1.0.0"}, externalDependencies=universal)
    assert hint in run(directory, "install", "-y", ok=False).stderr
    untouched(directory)
    script = directory / "build.sh"
    script.write_text(run(directory, "gen-script", "-shell", "sh").stdout)
    failed = subprocess.run([tools / "sh", script], cwd=directory, env=env,
                            capture_output=True, text=True, timeout=30)
    assert failed.returncode != 0 and hint in failed.stderr, failed
    assert not (directory / "build/install/early-marker").exists()

print("External dependency preflight passed: files, hints, platforms, tools, scripts and update preservation")
