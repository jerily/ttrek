"""Download nested archives through native install and portable Bash/POSIX scripts."""
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
with tempfile.TemporaryDirectory(prefix="ttrek download ' ") as temporary:
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
        'cmake_minimum_required(VERSION 3.22)\nproject(archives NONE)\n'
        f'file(READ "${{CMAKE_CURRENT_SOURCE_DIR}}/{destination}/value.txt" value)\n'
        'if(NOT value STREQUAL "nested archive")\nmessage(FATAL_ERROR "Wrong source")\nendif()\n'
        f'install(FILES "${{CMAKE_CURRENT_SOURCE_DIR}}/{destination}/value.txt" DESTINATION share)\n')
    archive = root / "source.tar.gz"
    with tarfile.open(archive, "w:gz") as output:
        output.add(source, arcname="source")
    nested = root / "nested.zip"
    with zipfile.ZipFile(nested, "w") as output:
        output.writestr("nested/value.txt", "nested archive")
    bootstrap = root / "CMakeLists.txt"
    bootstrap.write_text('cmake_minimum_required(VERSION 3.22)\nproject(wrapper NONE)\n'
                         'add_subdirectory("${ARCHIVE_SOURCE}" source)\n')
    nested_name = "archives/nested ' $literal.zip"
    steps = [
        {"cmd": "download", "url": archive.as_uri()}, {"cmd": "unpack"},
        {"cmd": "download", "url": nested.as_uri(), "filename": nested_name},
        {"cmd": "unpack", "filename": nested_name, "destination": destination},
        {"cmd": "download", "url": bootstrap.as_uri(), "filename": "cmake/CMakeLists.txt"},
        {"cmd": "cd"},
        {"cmd": "cmake_config", "source": "$DOWNLOAD_DIR/cmake", "options": [
            {"name": "ARCHIVE_SOURCE", "value": "$SOURCE_DIR"}]},
        {"cmd": "cmake_install"}]
    directory = registry / "download-demo/1.0.0"
    directory.mkdir(parents=True)
    recipe = {"name": "download-demo", "version": "1.0.0", "dependencies": {}, "build": {"default": steps}}
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
        generated = run("gen-script", "-shell", shell, "download-demo@1.0.0").stdout
        assert 'SOURCE_DIR="$SOURCE_DIR/' not in generated
        script.write_text(generated)
        result = subprocess.run([shell, script], env=env, capture_output=True, text=True, timeout=60)
        assert result.returncode == 0, (result.stdout, result.stderr,
            [str(p.relative_to(output)) for p in output.rglob("*")],
            [line for line in generated.splitlines() if "vendor" in line or "nested" in line],
            (output / "build/logs/download-demo-1.0.0/unpack.log").read_text())
        assert (output / "build/install/share/value.txt").read_text() == "nested archive"
    run("install", "-y", "download-demo@1.0.0")
    assert (project / "ttrek-venv/local/share/value.txt").read_text() == "nested archive"

    for step, field, values in [
        (0, "url", [None, ""]),
        (0, "filename", [None, "", "../escape", ".", "/tmp/escape", "nested/../escape", "C:\\escape"]),
        (1, "filename", [None, "../escape"]),
        (3, "destination", [None, "", "/tmp/escape", "nested/../escape", "C:\\escape"]),
    ]:
        for value in values:
            broken = copy.deepcopy(recipe)
            broken["build"]["default"][step][field] = value
            recipe_file.write_text(json.dumps(broken))
            result = run("gen-script", "download-demo@1.0.0", ok=False)
            assert ("URL" if field == "url" else field) in result.stderr, result.stderr
            assert not result.stdout
    recipe_file.write_text(json.dumps(recipe))
    # Source retrieval is skipped for a local project build, just like download/unpack.
    local = json.loads((project / "ttrek.json").read_text())
    local["build"] = {"default": [{"cmd": "download", "url": "file:///must-not-download"}]}
    (project / "ttrek.json").write_text(json.dumps(local))
    assert "must-not-download" not in run("gen-script").stdout
print("download: native install, Bash/sh, nested ZIP, quoting, validation, local-build skip passed")
