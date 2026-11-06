"""Check installed artifacts and build order using a disposable local registry."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile

BINARY, CMAKE, CURL = (Path(arg).absolute() for arg in sys.argv[1:])

with tempfile.TemporaryDirectory(prefix="ttrek-install-options-") as temporary:
    root = Path(temporary)
    registry = root / "registry"
    project = root / "project"
    project.mkdir()
    home = root / "home"
    home.mkdir()
    tools = root / "tools"
    tools.mkdir()
    (tools / "cmake").symlink_to(CMAKE)
    (tools / "curl").symlink_to(CURL)
    env = dict(os.environ, HOME=str(home), CURL_HOME=str(home),
               PATH=str(tools) + os.pathsep + os.environ["PATH"], SHELL="/bin/sh",
               TTREK_NO_UPDATE_CHECK="1", TTREK_FAIL_VERBOSE="1",
               http_proxy="http://127.0.0.1:9", https_proxy="http://127.0.0.1:9")

    def run(*args, cwd=project, ok=True):
        result = subprocess.run([BINARY, *args], cwd=cwd, env=env, text=True,
                                capture_output=True, timeout=60)
        assert (result.returncode == 0) == ok, (args, result.stdout, result.stderr)
        return result

    def recipe(name, dependencies, version="1.0.0"):
        source = root / f"{name}-{version}"
        source.mkdir()
        (source / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.22)\nproject(fixture LANGUAGES NONE)\n"
            f'file(APPEND "${{CMAKE_INSTALL_PREFIX}}/../build-order.log" "{name}@{version}\\n")\n'
            f'file(WRITE "${{CMAKE_CURRENT_BINARY_DIR}}/{name}.txt" "{version}")\n'
            f'install(FILES "${{CMAKE_CURRENT_BINARY_DIR}}/{name}.txt" DESTINATION share)\n')
        archive = source.with_suffix(".tar.gz")
        with tarfile.open(archive, "w:gz") as output:
            output.add(source, arcname=source.name)
        directory = registry / name / version
        directory.mkdir(parents=True)
        (directory / "ttrek.json").write_text(json.dumps({
            "name": name, "version": version, "dependencies": dependencies,
            "build": {"default": [
                {"cmd": "download", "url": archive.as_uri()}, {"cmd": "unpack"},
                {"cmd": "cd"}, {"cmd": "cmake_config"},
                {"cmd": "cmake_make"}, {"cmd": "cmake_install"}]}}))

    graph = {"helper": {}, "base": {"helper": "^1.0.0"},
             "middle": {"base": "^1.0.0"}, "top": {"middle": "^1.0.0"},
             "unrelated": {"helper": "^1.0.0"}, "devtool": {"helper": "^1.0.0"}}
    for name, dependencies in graph.items():
        recipe(name, dependencies)

    run("init", "-y", "-registry", str(registry))
    spec_path = project / "ttrek.json"
    build_log = project / "ttrek-venv/build-order.log"

    def built(*args):
        if build_log.exists():
            build_log.unlink()
        run(*args)
        return build_log.read_text().splitlines() if build_log.exists() else []

    built("install", "-y", "top", "unrelated")
    assert json.loads(spec_path.read_text())["dependencies"] == {
        "top": "^1.0.0", "unrelated": "^1.0.0"}
    assert built("install", "-y", "-force", "base") == [
        "base@1.0.0", "middle@1.0.0", "top@1.0.0"]
    assert "middle" not in json.loads(spec_path.read_text())["dependencies"]
    assert built("install", "-y", "top") == []

    recipe("base", graph["base"], "1.1.0")
    recipe("unrelated", graph["unrelated"], "1.1.0")
    assert built("update", "-y", "base") == [
        "base@1.1.0", "middle@1.0.0", "top@1.0.0"]
    assert (project / "ttrek-venv/local/share/unrelated.txt").read_text() == "1.0.0"
    assert built("install", "-y", "--save-dev", "devtool") == ["devtool@1.0.0"]
    assert built("install", "-y", "-D", "top") == []
    spec = json.loads(spec_path.read_text())
    assert built("install", "-y", "--save-dev") == []
    assert json.loads(spec_path.read_text()) == spec
    assert "Nothing to install!" in run("pretend").stdout
    assert spec["devDependencies"] == {"devtool": "^1.0.0", "top": "^1.0.0"}
    assert "top" not in spec["dependencies"] and "helper" not in spec["dependencies"]

    recipe("devtool", graph["devtool"], "1.1.0")
    assert built("update", "-y", "devtool") == ["devtool@1.1.0"]
    assert "devtool" in json.loads(spec_path.read_text())["devDependencies"]
    assert "devtool" not in json.loads(spec_path.read_text())["dependencies"]
    assert built("install", "-y", "-force", "top") == ["top@1.0.0"]
    assert "top" in json.loads(spec_path.read_text())["devDependencies"]

    # A fresh install must resolve both sections without promoting transitive deps.
    fresh = root / "fresh"
    fresh.mkdir()
    (fresh / "ttrek.json").write_text(spec_path.read_text())
    run("install", "-y", "-registry", str(registry), cwd=fresh)
    assert (fresh / "ttrek-venv/local/share/devtool.txt").exists()
    assert json.loads((fresh / "ttrek.json").read_text()) == json.loads(spec_path.read_text())
    assert built("update", "-y") == ["unrelated@1.1.0"]
    run("uninstall", "-y", "devtool")
    assert "devtool" not in json.loads(spec_path.read_text())["devDependencies"]

    # Reach every reverse dependency even through chains longer than ten packages.
    deep = root / "deep"
    deep.mkdir()
    packages = {}
    for index in range(13):
        name = f"chain{index}"
        dependencies = {f"chain{index - 1}": "1.0.0"} if index else {}
        directory = registry / name / "1.0.0"
        directory.mkdir(parents=True)
        (directory / "ttrek.json").write_text(json.dumps({"dependencies": dependencies}))
        packages[name] = {"version": "1.0.0", "requires": dependencies}
    (deep / "ttrek.json").write_text(json.dumps({"dependencies": {"chain12": "1.0.0"}}))
    (deep / "ttrek-lock.json").write_text(json.dumps({"registry": str(registry), "packages": packages}))
    plan = run("pretend", "-force", "chain0", cwd=deep).stdout
    for index in range(13):
        assert f"chain{index}@1.0.0" in plan, plan

    assert "invalid strategy" in run("install", "-strategy", "invalid", ok=False).stderr
    for flag in ("--save-dev", "-D", "-save-dev", "-force", "-strategy", "-fail-verbose", "-y"):
        assert flag in run("help", "install").stdout

print("Install options passed: dev dependencies, targeted rebuilds/updates, ordering, fresh installs, and deep reverse dependencies")
