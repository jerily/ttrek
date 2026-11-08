"""Fetch nested archives through native install and portable Bash/POSIX scripts."""
import copy
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import zipfile

binary, cmake, curl = (Path(arg).absolute() for arg in sys.argv[1:])
with tempfile.TemporaryDirectory(prefix="ttrek fetch ' ") as temporary:
    root = Path(temporary)
    registry = root / "registry"
    project = root / "project"
    project.mkdir()
    bin_dir = root / "bin"
    bin_dir.mkdir()
    (bin_dir / "cmake").symlink_to(cmake)
    (bin_dir / "curl").symlink_to(curl)
    env = dict(os.environ, TTREK_NO_UPDATE_CHECK="1", TTREK_FAIL_VERBOSE="1",
               PATH=str(bin_dir) + os.pathsep + os.environ["PATH"], SHELL="/bin/sh")
    source = root / "source"
    source.mkdir()
    destination = "vendor ' $literal"
    (source / "CMakeLists.txt").write_text(
        'cmake_minimum_required(VERSION 3.22)\nproject(fetch NONE)\n'
        f'file(READ "${{CMAKE_CURRENT_SOURCE_DIR}}/{destination}/value.txt" value)\n'
        'if(NOT value STREQUAL "nested archive")\nmessage(FATAL_ERROR "Wrong source")\nendif()\n'
        f'install(FILES "${{CMAKE_CURRENT_SOURCE_DIR}}/{destination}/value.txt" DESTINATION share)\n')
    archive = root / "source.tar.gz"
    with tarfile.open(archive, "w:gz") as output:
        output.add(source, arcname="source")
    nested = root / "nested.zip"
    with zipfile.ZipFile(nested, "w") as output:
        output.writestr("nested/value.txt", "nested archive")
    steps = [
        {"cmd": "fetch", "url": archive.as_uri()},
        {"cmd": "fetch", "url": nested.as_uri(), "archive": "nested ' $literal.zip", "destination": destination},
        {"cmd": "cd"}, {"cmd": "cmake_config"}, {"cmd": "cmake_install"}]
    directory = registry / "fetch-demo/1.0.0"
    directory.mkdir(parents=True)
    recipe = {"name": "fetch-demo", "version": "1.0.0", "dependencies": {}, "build": {"default": steps}}
    recipe_file = directory / "ttrek.json"
    recipe_file.write_text(json.dumps(recipe))

    def run(*args, ok=True):
        result = subprocess.run([binary, *args], cwd=project, env=env, text=True,
                                capture_output=True, timeout=60)
        assert (result.returncode == 0) == ok, (args, result.stdout, result.stderr)
        return result

    run("init", "-y", "-registry", str(registry))
    for shell in ("bash", "sh"):
        output = root / shell
        output.mkdir()
        script = output / "build.sh"
        generated = run("gen-script", "-shell", shell, "fetch-demo@1.0.0").stdout
        assert 'SOURCE_DIR="$SOURCE_DIR/' not in generated
        script.write_text(generated)
        result = subprocess.run([shell, script], env=env, capture_output=True, text=True, timeout=60)
        assert result.returncode == 0, (result.stdout, result.stderr,
            [str(p.relative_to(output)) for p in output.rglob("*")],
            [line for line in generated.splitlines() if "vendor" in line or "nested" in line],
            (output / "build/logs/fetch-demo-1.0.0/unpack.log").read_text())
        assert (output / "build/install/share/value.txt").read_text() == "nested archive"
    run("install", "-y", "fetch-demo@1.0.0")
    assert (project / "ttrek-venv/local/share/value.txt").read_text() == "nested archive"

    for field, value in [("url", None), ("url", ""), ("archive", None), ("archive", ""),
                         ("archive", "../escape"), ("archive", "nested/file"), ("archive", "."),
                         ("destination", None), ("destination", ""), ("destination", "/tmp/escape"),
                         ("destination", "nested/../escape"), ("destination", "C:\\escape")]:
        broken = copy.deepcopy(recipe)
        broken["build"]["default"][0][field] = value
        recipe_file.write_text(json.dumps(broken))
        result = run("gen-script", "fetch-demo@1.0.0", ok=False)
        assert "fetch requires" in result.stderr, result.stderr
        assert not result.stdout
    recipe_file.write_text(json.dumps(recipe))
    # Source retrieval is skipped for a local project build, just like download/unpack.
    local = json.loads((project / "ttrek.json").read_text())
    local["build"] = {"default": [{"cmd": "fetch", "url": "file:///must-not-fetch"}]}
    (project / "ttrek.json").write_text(json.dumps(local))
    assert "must-not-fetch" not in run("gen-script").stdout
print("fetch: native install, Bash/sh, nested ZIP, quoting, validation, local-build skip passed")
