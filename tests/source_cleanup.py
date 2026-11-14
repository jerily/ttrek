"""Native source cleanup, shared reuse, -keep, failures and local builds."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile

BINARY, CMAKE, CURL = (Path(arg).absolute() for arg in sys.argv[1:])

with tempfile.TemporaryDirectory(prefix="ttrek cleanup ' $ ") as temporary:
    root = Path(temporary)
    registry = root / "registry"
    registry.mkdir()
    home = root / "home"
    home.mkdir()
    tools = root / "tools"
    tools.mkdir()
    (tools / "cmake").symlink_to(CMAKE)
    (tools / "curl").symlink_to(CURL)
    env = dict(os.environ, HOME=str(home), CURL_HOME=str(home), SHELL="/bin/sh",
               PATH=str(tools) + os.pathsep + os.environ["PATH"],
               TTREK_NO_UPDATE_CHECK="1", TTREK_FAIL_VERBOSE="1", IS_TTY="0")
    source = root / "source"
    source.mkdir()
    (source / "value.txt").write_text("original\n")
    (source / "CMakeLists.txt").write_text('''
cmake_minimum_required(VERSION 3.22)
project(cleanup NONE)
set(work "${CMAKE_INSTALL_PREFIX}/../build")
set(marker "${CMAKE_CURRENT_SOURCE_DIR}/../consumer")
if(SERVICE STREQUAL "second")
  foreach(directory source named-source build)
    set(previous "${work}/${directory}/first-1.0.0")
    if(EXPECT_CLEAN AND EXISTS "${previous}")
      message(FATAL_ERROR "Previous package was not cleaned: ${previous}")
    elseif(NOT EXPECT_CLEAN AND NOT EXISTS "${previous}")
      message(FATAL_ERROR "Previous package was not kept: ${previous}")
    endif()
  endforeach()
  # The first consumer leaves a marker beside the read-only source tree.
  # Re-extraction or cleanup between consumers would lose it.
  file(READ "${marker}" consumer)
  if(NOT consumer STREQUAL "first")
    message(FATAL_ERROR "Shared source was not reused")
  endif()
else()
  file(WRITE "${marker}" "first")
endif()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${SERVICE}.txt" "installed")
if(FAIL_BUILD)
  message(FATAL_ERROR "Intentional build failure")
endif()
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${SERVICE}.txt" DESTINATION share)
''')
    archive = root / "source.tar.gz"
    with tarfile.open(archive, "w:gz") as output:
        output.add(source, arcname="source")

    def recipes(*, keep=False, fail=False):
        for name in ("first", "second"):
            directory = registry / name / "1.0.0"
            directory.mkdir(parents=True, exist_ok=True)
            (directory / "value.diff").write_text(
                "--- a/value.txt\n+++ b/value.txt\n@@ -1 +1 @@\n-original\n+patched\n")
            (directory / "ttrek.json").write_text(json.dumps({
                "name": name, "version": "1.0.0",
                "dependencies": {"first": "1.0.0"} if name == "second" else {},
                "build": {"default": [
                    {"cmd": "download", "url": archive.as_uri(), "filename": "nested/sdk.tar.gz"},
                    {"cmd": "unpack", "filename": "nested/sdk.tar.gz"},
                    {"cmd": "unpack", "filename": "nested/sdk.tar.gz", "as": "work", "select": True},
                    {"cmd": "patch", "filename": "value.diff", "p_num": "1"},
                    {"cmd": "unpack", "filename": "nested/sdk.tar.gz", "as": "sdk",
                     "shared": "sdk-1.0", "select": True},
                    {"cmd": "cd"},
                    {"cmd": "cmake_config", "options": [
                        {"name": "SERVICE", "value": name},
                        {"name": "EXPECT_CLEAN", "value": "OFF" if keep else "ON"},
                        {"name": "FAIL_BUILD", "value": "ON" if fail and name == "second" else "OFF"}]},
                    {"cmd": "cmake_install"}]}}))

    def run(project, *args, ok=True, input=None):
        result = subprocess.run([BINARY, *args], cwd=project, env=env, text=True,
                                input=input, capture_output=True, timeout=90)
        assert (result.returncode == 0) == ok, (args, result.stdout, result.stderr)
        assert "could not clean up" not in result.stderr, result.stderr
        return result

    def init(name):
        project = root / name
        project.mkdir()
        run(project, "init", "-y", "-registry", str(registry))
        return project

    def private_files(build, name, kept):
        for directory in ("source", "named-source", "build"):
            assert (build / directory / f"{name}-1.0.0").exists() == kept, (directory, name, kept)
        assert (build / f"source/patch-{name}-1.0.0-value.diff").exists() == kept
        assert (build / f"logs/{name}-1.0.0/configure.log").exists()
        assert (build / f"install-{name}-1.0.0.sh").exists()

    for command in ("install", "update"):
        for keep in (False, True):
            project = init(f"{command}-{keep}")
            if command == "update":
                recipes(keep=True)
                run(project, "install", "-y", "-keep", "second")
            recipes(keep=keep)
            run(project, command, "-y", "-force", *(["-keep"] if keep else []), "first", "second")
            build = project / "ttrek-venv/build"
            for name in ("first", "second"):
                private_files(build, name, keep)
                assert (project / f"ttrek-venv/local/share/{name}.txt").read_text() == "installed"
            assert (build / "download/nested/sdk.tar.gz").exists() == keep
            assert (build / "shared-source").exists() == keep
            if keep:
                shared = next((build / "shared-source").rglob(".complete")).parent / "source"
                assert shared.stat().st_mode & 0o222 == 0
                # A no-op or declined operation must not erase previously kept files.
                run(project, command, "-y", "second")
                run(project, command, "-force", "first", input="n\n")
                for name in ("first", "second"):
                    private_files(build, name, True)
                assert shared.exists() and (build / "download/nested/sdk.tar.gz").exists()

        recipes(fail=True)
        project = init(f"{command}-failure")
        run(project, command, "-y", "second", ok=False)
        build = project / "ttrek-venv/build"
        private_files(build, "first", False)
        private_files(build, "second", True)
        assert (build / "download/nested/sdk.tar.gz").exists()
        assert list((build / "shared-source").rglob(".complete"))

    # Shared cleanup must wait for the root build and postInstall hook, while
    # leaving the project's own source tree and build directory intact.
    recipes()
    for failure in ("build", "hook", None):
        project = init(f"local-{failure}")
        build = project / "ttrek-venv/build"
        outside = project / "outside"
        outside.mkdir()
        (outside / "keep.txt").write_text("outside")
        outside.chmod(0o555)
        (project / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.22)\nproject(root NONE)\n'
            'if(NOT EXISTS "${CMAKE_INSTALL_PREFIX}/../build/shared-source")\n'
            '  message(FATAL_ERROR "Shared sources removed before local build")\nendif()\n'
            + ('message(FATAL_ERROR "Intentional root build failure")\n' if failure == "build" else ''))
        hook = project / "post-install.sh"
        hook.write_text('#!/bin/sh\nset -e\n'
                        '[ -d "$PROJECT_HOME/ttrek-venv/build/shared-source" ]\n'
                        'ln -s "$PROJECT_HOME/outside" "$PROJECT_HOME/ttrek-venv/build/shared-source/outside"\n'
                        + ('exit 1\n' if failure == "hook" else ''))
        hook.chmod(0o755)
        spec = json.loads((project / "ttrek.json").read_text())
        spec.update({"name": "root", "version": "1.0.0", "dependencies": {"second": "1.0.0"},
                     "scripts": {"postInstall": {"file": "post-install.sh"}},
                     "build": {"default": [{"cmd": "cd"}, {"cmd": "cmake_config"}]}})
        (project / "ttrek.json").write_text(json.dumps(spec))
        run(project, "install", "-y", ok=failure is None)
        assert (project / "CMakeLists.txt").exists() and hook.exists()
        assert (build / "build/root-1.0.0/CMakeCache.txt").exists()
        assert (outside / "keep.txt").read_text() == "outside"
        assert outside.stat().st_mode & 0o222 == 0
        assert (build / "shared-source").exists() == (failure is not None)
        assert (build / "download").exists() == (failure is not None)
        outside.chmod(0o755)

print("Source cleanup passed: per-package cleanup, shared reuse, keep, no-op, failure and project preservation")
