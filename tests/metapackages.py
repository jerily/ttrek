"""Migrating a file-owning bundle to dependencies must preserve ownership."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile

binary, cmake, curl = (Path(arg).absolute() for arg in sys.argv[1:])
with tempfile.TemporaryDirectory(prefix="ttrek-metapackages-") as temporary:
    root = Path(temporary)
    registry = root / "registry"
    project = root / "project"
    project.mkdir()
    tools = root / "bin"
    tools.mkdir()
    (tools / "cmake").symlink_to(cmake)
    (tools / "curl").symlink_to(curl)
    env = dict(os.environ, TTREK_NO_UPDATE_CHECK="1", TTREK_FAIL_VERBOSE="1",
               PATH=str(tools) + os.pathsep + os.environ["PATH"], SHELL="/bin/sh")

    def run(*args, ok=True):
        result = subprocess.run([binary, *args], cwd=project, env=env, text=True,
                                capture_output=True, timeout=60)
        assert (result.returncode == 0) == ok, (args, result.stdout, result.stderr)
        return result

    def recipe(name, dependencies, filename=None, content="new"):
        directory = registry / name / "1.0.0"
        directory.mkdir(parents=True, exist_ok=True)
        steps = []
        if filename:
            source = root / name
            source.mkdir()
            (source / "CMakeLists.txt").write_text(
                'cmake_minimum_required(VERSION 3.22)\nproject(fixture NONE)\n'
                f'file(WRITE "${{CMAKE_CURRENT_BINARY_DIR}}/{filename}" "{content}")\n'
                f'install(FILES "${{CMAKE_CURRENT_BINARY_DIR}}/{filename}" DESTINATION share)\n')
            archive = root / (name + ".tar.gz")
            with tarfile.open(archive, "w:gz") as output:
                output.add(source, arcname=name)
            steps = [{"cmd": "fetch", "url": archive.as_uri()}, {"cmd": "cd"},
                     {"cmd": "cmake_config"}, {"cmd": "cmake_install"}]
        spec = {"name": name, "version": "1.0.0", "dependencies": dependencies, "build": {"default": steps}}
        (directory / "ttrek.json").write_text(json.dumps(spec))
        return spec

    recipe("bundle", {}, "shared.txt", "old")
    recipe("core", {}, "shared.txt")
    recipe("service", {"core": "1.0.0"}, "service.txt")
    run("init", "-y", "-registry", str(registry))
    run("install", "-y", "bundle@1.0.0")
    prefix = project / "ttrek-venv/local"
    assert (prefix / "share/shared.txt").read_text() == "old"

    # A failed split restores the old bundle's files; the next invocation also
    # exercises ttrek's normal dirty-install recovery.
    broken = recipe("broken", {})
    broken["build"]["default"] = [{"cmd": "unknown-command"}]
    (registry / "broken/1.0.0/ttrek.json").write_text(json.dumps(broken))
    recipe("bundle", {"broken": "1.0.0"})
    run("install", "-y", ok=False)
    assert (prefix / "share/shared.txt").read_text() == "old"

    # Same version, new dependency graph: the registry's previously bundled
    # release can become a metapackage without leaving stale file ownership.
    recipe("bundle", {"service": "1.0.0"})
    run("install", "-y")
    assert (prefix / "share/shared.txt").read_text() == "new"
    manifest = json.loads((project / "ttrek-venv/ttrek-manifest.json").read_text())
    assert manifest["bundle"]["files"] == []
    assert "share/shared.txt" in manifest["core"]["files"]
    assert "share/service.txt" in manifest["service"]["files"]
    run("uninstall", "-y", "bundle")
    assert (prefix / "share/shared.txt").is_file()
    assert (prefix / "share/service.txt").is_file()
    run("uninstall", "-y", "service")
    assert (prefix / "share/shared.txt").is_file()
    assert not (prefix / "share/service.txt").exists()
print("metapackage migration: rollback, same-version split, ownership and independent removal passed")
