"""Exercise the production CLI against a directory registry, with HTTP unavailable."""
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

BINARY = Path(sys.argv[1]).resolve()
CMAKE = Path(sys.argv[2]).resolve()

with tempfile.TemporaryDirectory(prefix="ttrek local registry ") as temporary:
    root = Path(temporary)
    registry = root / "registry's files"
    registry.mkdir()
    project = root / "project"
    project.mkdir()
    relative_registry = "../registry's files"
    env = dict(os.environ, PATH=str(CMAKE.parent) + os.pathsep + os.environ["PATH"],
               http_proxy="http://127.0.0.1:9", https_proxy="http://127.0.0.1:9",
               ALL_PROXY="http://127.0.0.1:9", NO_PROXY="", no_proxy="", TTREK_MAKE_THREADS="2")

    def run(*args, cwd=project, ok=True):
        result = subprocess.run([BINARY, *args], cwd=cwd, env=env,
                                text=True, capture_output=True, timeout=60)
        assert (result.returncode == 0) == ok, (args, result.returncode, result.stdout, result.stderr)
        return result

    recipes = {}
    for name, dependencies in (("base", {}), ("app", {"base": "^1.0.0"})):
        source = root / name
        source.mkdir()
        (source / "value.c").write_text("int value(void) { return 41; }\n")
        (source / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.22)\nproject(" + name + " C)\n"
            "option(BUILD_SHARED_LIBS \"Shared\" ON)\n"
            + ("if(NOT EXISTS \"${CMAKE_INSTALL_PREFIX}/include/base.h\")\n"
               "message(FATAL_ERROR \"Missing base dependency\")\nendif()\n" if name == "app" else "")
            + "add_library(" + name + " value.c)\n"
            "install(TARGETS " + name + " LIBRARY DESTINATION lib ARCHIVE DESTINATION lib)\n"
            "install(FILES value.c DESTINATION include RENAME " + name + ".h)\n")
        archive = root / (name + ".tar.gz")
        with tarfile.open(archive, "w:gz") as output:
            output.add(source, arcname=name)
        directory = registry / name / "1.0.0"
        directory.mkdir(parents=True)
        steps = [{"cmd": "download", "url": archive.as_uri()}, {"cmd": "unpack"}]
        if name == "base":
            (directory / "value.diff").write_text(
                "--- a/value.c\n+++ b/value.c\n@@ -1 +1 @@\n"
                "-int value(void) { return 41; }\n+int value(void) { return 42; }\n")
            steps += [{"cmd": "patch", "filename": "value.diff", "p_num": "1"}]
        steps += [{"cmd": "cd"}, {"cmd": "cmake_config", "options": [
            {"name": "BUILD_SHARED_LIBS", "value": "OFF", "if": "+static"}]},
                  {"cmd": "cmake_make", "parallel": True}, {"cmd": "cmake_install"}]
        build = {"default": steps}
        if name == "app":
            build = {"default": [{"cmd": "invalid-fallback"}],
                     (platform.system() + "." + platform.machine()).lower(): steps}
        recipes[name] = {"name": name, "version": "1.0.0", "dependencies": dependencies, "build": build}
        (directory / "ttrek.json").write_text(json.dumps(recipes[name]))

    # Bad options fail before init creates anything; both placements are accepted.
    run("init", "-y", "-registry", str(root / "missing"), ok=False)
    assert not list(project.iterdir())
    run("-registry", ok=False)
    for command in ("init", "gen-script", "pretend", "install", "update"):
        run(command, "-registry", ok=False)
    run("init", "-y", "-registry", relative_registry)
    spec_file = project / "ttrek.json"
    lock_file = project / "ttrek-lock.json"
    spec = json.loads(spec_file.read_text())
    spec["dependencies"] = {"app": "^1.0.0"}
    spec_file.write_text(json.dumps(spec))
    assert json.loads(lock_file.read_text())["registry"] == relative_registry
    assert not (project / "ttrek-venv/machine-id").exists()

    before = {p.relative_to(project): p.read_bytes() for p in project.rglob("*") if p.is_file()}
    default = run("gen-script").stdout
    assert "'BUILD_SHARED_LIBS'=\"OFF\"" not in default
    assert default == run("gen-script", "-registry", relative_registry).stdout
    assert default == run("-registry", relative_registry, "gen-script").stdout
    assert "app@1.0.0" in run("pretend").stdout
    nested = project / "nested"
    nested.mkdir()
    assert default == run("gen-script", cwd=nested).stdout
    assert before == {p.relative_to(project): p.read_bytes() for p in project.rglob("*") if p.is_file()}

    generated = run("gen-script", "-static").stdout
    assert str(registry) not in generated
    build_script = project / "build.sh"
    build_script.write_text(generated)
    built = subprocess.run(["bash", build_script], cwd=root, env=env, capture_output=True, text=True, timeout=60)
    assert built.returncode == 0, (built.stdout, built.stderr)
    assert (project / "build/install/lib/libapp.a").exists()
    assert "return 42" in (project / "build/install/include/base.h").read_text()

    # Install and update use saved registry metadata and retain the setting.
    run("install", "-y")
    lock = json.loads(lock_file.read_text())
    assert lock["registry"] == relative_registry
    assert lock["packages"]["base"]["version"] == "1.0.0"
    assert "+static" in lock["packages"]["base"]["iuse"]
    assert "return 42" in (project / "ttrek-venv/local/include/base.h").read_text()
    shutil.copytree(registry / "base/1.0.0", registry / "base/1.1.0")
    newer = copy.deepcopy(recipes["base"])
    newer["version"] = "1.1.0"
    (registry / "base/1.1.0/ttrek.json").write_text(json.dumps(newer))
    run("update", "-y", "-registry", relative_registry)
    lock = json.loads(lock_file.read_text())
    assert lock["packages"]["base"]["version"] == "1.1.0"
    assert lock["registry"] == relative_registry

    # An explicit directory overrides a stale lock setting without rewriting it.
    lock["registry"] = "../missing"
    lock_file.write_text(json.dumps(lock))
    assert not run("gen-script", ok=False).stdout
    assert run("gen-script", "-registry", relative_registry).stdout.startswith("#!")
    assert json.loads(lock_file.read_text())["registry"] == "../missing"
    lock["registry"] = 42
    lock_file.write_text(json.dumps(lock))
    assert "registry must be" in run("gen-script", ok=False).stderr
    lock["registry"] = relative_registry
    lock_file.write_text(json.dumps(lock))

    # Missing/malformed local data never falls back to HTTP or emits a partial script.
    manifest = registry / "app/1.0.0/ttrek.json"
    original = manifest.read_text()
    manifest.write_text('{"dependencies": {}} trailing garbage')
    failed = run("gen-script", ok=False)
    assert not failed.stdout and "Invalid JSON" in failed.stderr
    manifest.write_text(json.dumps({"dependencies": {}, "build": {"other.platform": []}}))
    failed = run("gen-script", ok=False)
    assert not failed.stdout and "No build instruction array" in failed.stderr
    manifest.write_text(original)
    spec["dependencies"] = {"..": "1.0.0"}
    spec_file.write_text(json.dumps(spec))
    lock_file.write_text(json.dumps({"registry": relative_registry}))
    failed = run("gen-script", ok=False)
    assert not failed.stdout and "Invalid registry path component" in failed.stderr

    # Global init options persist too, while ordinary init keeps the HTTP default.
    for args in (("-registry", str(registry), "init", "-y"), ("init", "-y")):
        fresh = root / ("global-init" if args[0] == "-registry" else "default-init")
        fresh.mkdir()
        run(*args, cwd=fresh)
        saved = json.loads((fresh / "ttrek-lock.json").read_text())
        assert saved.get("registry") == (str(registry) if args[0] == "-registry" else None)

print("Local registry generation, init persistence, install/update, patches and errors passed")
