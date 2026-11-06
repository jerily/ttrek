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
CURL = Path(sys.argv[3]).absolute()

with tempfile.TemporaryDirectory(prefix="ttrek local registry ' ") as temporary:
    root = Path(temporary)
    home = root / "home"
    home.mkdir()
    tools = root / "bin"
    tools.mkdir()
    (tools / "cmake").symlink_to(CMAKE)
    (tools / "curl").symlink_to(CURL)
    registry = root / "registry's files"
    registry.mkdir()
    project = root / "project"
    project.mkdir()
    relative_registry = "../registry's files"
    # Isolate user registry/curl settings and include build logs on failure.
    env = dict(os.environ, HOME=str(home), CURL_HOME=str(home), XDG_CONFIG_HOME=str(home / ".config"),
               PATH=str(tools) + os.pathsep + os.environ["PATH"],
               TTREK_NO_UPDATE_CHECK="1", TTREK_FAIL_VERBOSE="1", SHELL="/bin/sh",
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
        recipes[name]["scripts"] = {
            "hello": "#!/bin/sh\nset -eu\nprintf '%s\\n' '" + name + "@1.0.0 café' \"$PROJECT_HOME\" \"$PROJECT_INSTALL_DIR\" \"$PWD\"\n"
                     "test -f \"$PROJECT_INSTALL_DIR/include/" + name + ".h\"\n",
            "preInstall": 'touch "$PROJECT_HOME/unexpected-package-hook"',
            "fail": "exit 7\n",
        }
        spec_recipe = copy.deepcopy(recipes[name])
        if name == "base":
            (directory / "hello.sh").write_text(spec_recipe["scripts"]["hello"])
            spec_recipe["scripts"]["hello"] = {"file": "hello.sh"}
        (directory / "ttrek.json").write_text(json.dumps(spec_recipe))

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
    spec["scripts"] = {"hello": "printf project-script", "preInstall": "printf pre > pre-hook"}
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
    assert not (project / "unexpected-package-hook").exists()
    assert (project / "ttrek-venv/local/pre-hook").read_text() == "pre"
    lock = json.loads(lock_file.read_text())
    assert lock["registry"] == relative_registry
    assert lock["packages"]["base"]["version"] == "1.0.0"
    assert "+static" in lock["packages"]["base"]["iuse"]
    assert "return 42" in (project / "ttrek-venv/local/include/base.h").read_text()
    manifest_file = project / "ttrek-venv/ttrek-manifest.json"
    installed = json.loads(manifest_file.read_text())
    assert installed["base"]["scripts"] == recipes["base"]["scripts"]
    assert installed["base"]["version"] == "1.0.0"

    # Installed scripts are independent of project scripts and registry availability.
    hidden_registry = root / "offline registry"
    registry.rename(hidden_registry)
    try:
        for package in ("base", "app"):
            assert set(run("scripts", "-package", package, "ls").stdout.splitlines()) == {"hello", "preInstall", "fail"}
            assert run("scripts", "-package", package, "describe", "hello").stdout == recipes[package]["scripts"]["hello"] + "\n"
            output = run("run", "-package", package, "-script", "hello", cwd=nested).stdout.splitlines()
            prefix = str(project / "ttrek-venv/local")
            assert output == [package + "@1.0.0 café", str(project), prefix, prefix], output
        assert run("run", "-script", "hello").stdout == "project-script"
        assert run("run", "-script", "HELLO").stdout == "project-script"
        assert run("run", "sh", "-c", "printf direct-command").stdout == "direct-command"
        # Stream bytes without re-encoding UTF-8 or truncating at NUL/chunk boundaries.
        streamed = subprocess.run([BINARY, "run", sys.executable, "-c",
                                   "import os; os.write(1, bytes(range(256)) * 100)"],
                                  cwd=project, env=env, capture_output=True, timeout=10, check=True)
        assert streamed.stdout == bytes(range(256)) * 100
        assert "7" in run("run", "-package", "base", "-script", "fail", ok=False).stderr
        assert not list((project / "ttrek-venv/tmp").glob("script-*.sh"))
        for args in (("run", "-package", "absent", "-script", "hello"),
                     ("scripts", "-package", "absent", "ls")):
            assert "not installed" in run(*args, ok=False).stderr
        for args in (("run", "-package", "base", "-script", "missing"),
                     ("scripts", "-package", "base", "describe", "missing")):
            assert "no named script" in run(*args, ok=False).stderr
        for args in (("run", "-package", "base"), ("run", "-package"),
                     ("run", "-package", "", "-script", "hello"),
                     ("run", "-package", "base", "-script", "hello", "unexpected"),
                     ("scripts", "-package"), ("scripts", "-package", "", "ls"),
                     ("scripts", "-package", "base", "add", "hello", "echo changed"),
                     ("scripts", "-package", "base", "del", "hello"),
                     ("run", "-u", "-g", "-package", "base", "-script", "hello")):
            run(*args, ok=False)
        assert json.loads(manifest_file.read_text()) == installed
        assert json.loads(spec_file.read_text())["scripts"] == spec["scripts"]

        # A lock entry alone is not proof of installation; legacy metadata gives guidance.
        for entry, message in ((None, "not installed"), ({"files": []}, "reinstall"),
                               ({"version": "1.0.0", "scripts": []}, "must be an object"),
                               ({"version": "1.0.0", "scripts": {"bad": 42}}, "string bodies")):
            changed = copy.deepcopy(installed)
            if entry is None:
                del changed["base"]
            else:
                changed["base"] = entry
            manifest_file.write_text(json.dumps(changed))
            assert message in run("scripts", "-package", "base", "ls", ok=False).stderr
        manifest_file.write_text(json.dumps(installed))
    finally:
        hidden_registry.rename(registry)

    # Desired lock flags cannot stand in for the flags used by the installed build.
    spec["useFlags"] = ["+static"]
    spec_file.write_text(json.dumps(spec))
    desired = json.loads(lock_file.read_text())
    for package in desired["packages"].values():
        package["use"] = ["+static"]
    lock_file.write_text(json.dumps(desired))
    assert "USE flags changed" in run("pretend").stdout
    run("install", "-y")
    assert (project / "ttrek-venv/local/lib/libbase.a").exists()
    assert json.loads(manifest_file.read_text())["base"]["use"] == ["+static"]
    assert "Nothing to install!" in run("pretend").stdout

    shutil.copytree(registry / "base/1.0.0", registry / "base/1.1.0")
    newer = copy.deepcopy(recipes["base"])
    newer["version"] = "1.1.0"
    newer["scripts"] = {"hello": "printf base@1.1.0"}
    (registry / "base/1.1.0/ttrek.json").write_text(json.dumps(newer))
    assert run("run", "-package", "base", "-script", "hello").stdout.startswith("base@1.0.0")
    run("update", "-y", "-registry", relative_registry)
    lock = json.loads(lock_file.read_text())
    assert lock["packages"]["base"]["version"] == "1.1.0"
    assert lock["registry"] == relative_registry
    assert run("run", "-package", "base", "-script", "hello").stdout == "base@1.1.0"
    assert run("scripts", "-package", "base", "ls").stdout == "hello\n"
    newer["scripts"] = {"hello": "printf refreshed"}
    (registry / "base/1.1.0/ttrek.json").write_text(json.dumps(newer))
    run("install", "-y", "-force", "base@1.1.0")
    assert run("run", "-package", "base", "-script", "hello").stdout == "refreshed"
    del newer["scripts"]
    (registry / "base/1.1.0/ttrek.json").write_text(json.dumps(newer))
    run("install", "-y", "-force", "base@1.1.0")
    assert run("scripts", "-package", "base", "ls").stdout == ""
    assert "no named script" in run("run", "-package", "base", "-script", "hello", ok=False).stderr

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
    bad = json.loads(original)
    bad["scripts"] = {"hello": 42}
    manifest.write_text(json.dumps(bad))
    before_manifest = manifest_file.read_bytes()
    assert "string bodies" in run("install", "-y", "-force", "app@1.0.0", ok=False).stderr
    assert manifest_file.read_bytes() == before_manifest
    # File references must stay inside the recipe directory and fail before execution.
    script_dir = manifest.parent
    (root / "outside.sh").write_text("echo outside\n")
    (script_dir / "linked.sh").symlink_to(root / "outside.sh")
    (script_dir / "nul.sh").write_bytes(b"echo before\x00echo after\n")
    (script_dir / "folder.sh").mkdir()
    for reference in ({"file": "missing.sh"}, {"file": "../outside.sh"},
                      {"file": str(root / "outside.sh")}, {"file": "linked.sh"},
                      {"file": "folder.sh"}, {"file": "nul.sh"},
                      {"file": 42}, {"file": "hello.sh", "extra": True}, {}):
        bad["scripts"] = {"hello": reference}
        manifest.write_text(json.dumps(bad))
        failed = run("gen-script", ok=False)
        assert not failed.stdout, (reference, failed.stdout)
        assert manifest_file.read_bytes() == before_manifest
    manifest.write_text(original)
    run("install", "-y", "-force", "app@1.0.0")
    run("uninstall", "-y", "app")
    assert "app" not in json.loads(manifest_file.read_text())
    assert "not installed" in run("run", "-package", "app", "-script", "hello", ok=False).stderr

    # User mode selects its own installed package, even from a different project.
    run("install", "-u", "-y", "-registry", str(registry), "app@1.0.0")
    user_project = home / ".ttrek"
    user_prefix = str(user_project / "ttrek-venv/local")
    assert run("run", "-u", "-package", "app", "-script", "hello").stdout.splitlines() == [
        "app@1.0.0 café", str(user_project), user_prefix, user_prefix]
    assert "hello" in run("scripts", "-u", "-package", "app", "ls").stdout.splitlines()
    assert "not installed" in run("scripts", "-package", "app", "ls", ok=False).stderr
    spec["dependencies"] = {"..": "1.0.0"}
    spec_file.write_text(json.dumps(spec))
    lock_file.write_text(json.dumps({"registry": relative_registry}))
    failed = run("gen-script", ok=False)
    assert not failed.stdout and "Invalid registry path component" in failed.stderr

    # Missing direct and transitive dependencies produce one actionable error.
    # Exercise every resolution command, without allowing a website fallback.
    missing = "web-blog"
    expected = (f'error: Package "{missing}" was not found in registry "{registry}". '
                'Check the package name or use -registry DIRECTORY to select another registry.\n')
    parent = registry / "parent/1.0.0"
    parent.mkdir(parents=True)
    (parent / "ttrek.json").write_text(json.dumps({"dependencies": {missing: "1.0.0"}}))
    for dependency in (missing, "parent"):
        for command in ("install", "update", "pretend", "gen-script"):
            fresh = root / f"missing-{dependency}-{command}"
            fresh.mkdir()
            (fresh / "ttrek.json").write_text(json.dumps({"dependencies": {dependency: "1.0.0"}}))
            failed = run(command, "-registry", str(registry), cwd=fresh, ok=False)
            assert failed.stderr == expected, (dependency, command, failed.stderr)
            if command == "gen-script":
                assert not failed.stdout

    # Corrupt entries keep their own diagnostic rather than being reported as absent.
    (registry / missing).write_text("not a package directory")
    failed = run("pretend", "-registry", str(registry), cwd=fresh, ok=False)
    assert "Package path is not a directory" in failed.stderr and "not found" not in failed.stderr
    (registry / missing).unlink()
    (registry / missing / "1.0.0").mkdir(parents=True)
    (registry / missing / "1.0.0/ttrek.json").write_text("invalid JSON")
    failed = run("pretend", "-registry", str(registry), cwd=fresh, ok=False)
    assert "Invalid JSON" in failed.stderr and "not found" not in failed.stderr
    assert failed.stderr.count("error:") == 1

    # Global init options persist too, while ordinary init keeps the HTTP default.
    for args in (("-registry", str(registry), "init", "-y"), ("init", "-y")):
        fresh = root / ("global-init" if args[0] == "-registry" else "default-init")
        fresh.mkdir()
        run(*args, cwd=fresh)
        saved = json.loads((fresh / "ttrek-lock.json").read_text())
        assert saved.get("registry") == (str(registry) if args[0] == "-registry" else None)

print("Local registry generation, init persistence, install/update, patches and errors passed")
