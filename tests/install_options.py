"""Check installed artifacts and build order using a disposable local registry."""
import json
import os
import shutil
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
    env.pop("TTREK_DEBUG", None)

    def run(*args, cwd=project, ok=True, process_env=env):
        result = subprocess.run([BINARY, *args], cwd=cwd, env=process_env, text=True,
                                capture_output=True, timeout=60)
        assert (result.returncode == 0) == ok, (args, result.stdout, result.stderr)
        if "TTREK_DEBUG" not in process_env:
            for detail in ("commit with OID:", "Hard reset to", "deleting file:", "deleting directory:"):
                assert detail not in result.stdout + result.stderr, result
        return result

    def recipe(name, dependencies, version="1.0.0", dev_dependencies=None):
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
            "devDependencies": dev_dependencies or {},
            "build": {"default": [
                {"cmd": "download", "url": archive.as_uri()}, {"cmd": "unpack"},
                {"cmd": "cd"}, {"cmd": "cmake_config"},
                {"cmd": "cmake_make"}, {"cmd": "cmake_install"}]}}))

    graph = {"helper": {}, "base": {"helper": "^1.0.0"},
             "middle": {"base": "^1.0.0"}, "top": {"middle": "^1.0.0"},
             "unrelated": {"helper": "^1.0.0"}, "devtool": {"helper": "^1.0.0"}}
    for name, dependencies in graph.items():
        recipe(name, dependencies)

    assert "Initialized package environment." in run("init", "-y", "-registry", str(registry)).stdout
    debug_env = dict(env, TTREK_DEBUG="")  # Presence, including an empty value, enables details.
    debug_project = root / "debug-project"
    debug_project.mkdir()
    assert "Created initial commit with OID:" in run(
        "init", "-y", "-registry", str(registry), cwd=debug_project, process_env=debug_env).stdout
    assert "Amended commit with OID:" in run(
        "install", "-y", "base", cwd=debug_project, process_env=debug_env).stdout
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

    # A root devDependency installs its regular dependencies recursively, but
    # none of the installed recipes' own devDependencies are followed. Missing
    # packages and impossible constraints there must not enter resolution.
    recipe("mock-service", {"helper": "^1.0.0"}, dev_dependencies={"missing-test-tool": "^1.0.0"})
    recipe("toolkit", {"mock-service": "^1.0.0"}, dev_dependencies={"helper": "^99.0.0"})
    assert built("install", "-y", "-D", "toolkit") == ["mock-service@1.0.0", "toolkit@1.0.0"]
    assert json.loads(spec_path.read_text())["devDependencies"] == {"toolkit": "^1.0.0"}
    installed = json.loads((project / "ttrek-venv/ttrek-manifest.json").read_text())
    locked = json.loads((project / "ttrek-lock.json").read_text())
    for name, dependencies in (("toolkit", {"mock-service": "^1.0.0"}), ("mock-service", {"helper": "^1.0.0"})):
        assert installed[name]["requires"] == locked["packages"][name]["requires"] == dependencies
    assert "missing-test-tool" not in installed
    assert built("install", "-y", "toolkit") == []
    assert built("install", "-y", "-force", "mock-service") == ["mock-service@1.0.0", "toolkit@1.0.0"]
    recipe("mock-service", {"helper": "^1.0.0"}, "1.1.0", dev_dependencies={"missing-test-tool": "^1.0.0"})
    assert built("update", "-y", "mock-service") == ["mock-service@1.1.0", "toolkit@1.0.0"]

    # The rule is identical whether toolkit is a root runtime or dev dependency.
    for section in ("dependencies", "devDependencies"):
        fresh = root / ("fresh-" + section)
        fresh.mkdir()
        fresh_spec = {"dependencies": {}, "devDependencies": {}}
        fresh_spec[section]["toolkit"] = "^1.0.0"
        (fresh / "ttrek.json").write_text(json.dumps(fresh_spec))
        plan = run("pretend", "-registry", str(registry), cwd=fresh).stdout
        assert "mock-service@1.1.0" in plan and "helper@1.0.0" in plan
        assert "missing-test-tool" not in plan
        assert not (fresh / "ttrek-venv").exists()
        generated = run("gen-script", "-registry", str(registry), cwd=fresh).stdout
        assert "mock-service" in generated and "helper" in generated
        assert "missing-test-tool" not in generated
        run("install", "-y", "-registry", str(registry), cwd=fresh)
        assert (fresh / "ttrek-venv/build-order.log").read_text().splitlines() == [
            "helper@1.0.0", "mock-service@1.1.0", "toolkit@1.0.0"]
        assert json.loads((fresh / "ttrek.json").read_text()) == fresh_spec
        fresh_installed = json.loads((fresh / "ttrek-venv/ttrek-manifest.json").read_text())
        assert set(fresh_installed) == {"helper", "mock-service", "toolkit"}

    # Restore the original fixture graph before the existing upgrade tests.
    run("uninstall", "-y", "toolkit", "mock-service")

    # Lock preferences and installed state are independent. A cloned lock still builds.
    cloned = root / "cloned"
    cloned.mkdir()
    for filename in ("ttrek.json", "ttrek-lock.json"):
        shutil.copyfile(project / filename, cloned / filename)
    clone_plan = run("pretend", cwd=cloned).stdout
    assert "Nothing to install!" not in clone_plan
    assert not (cloned / "ttrek-venv").exists()
    run("install", "-y", cwd=cloned)
    for name in ("helper", "base", "middle", "top", "unrelated"):
        assert (cloned / f"ttrek-venv/local/share/{name}.txt").read_text() == "1.0.0"

    manifest_path = project / "ttrek-venv/ttrek-manifest.json"
    manifest = json.loads(manifest_path.read_text())
    assert manifest["base"]["requires"] == {"helper": "^1.0.0"}
    assert manifest["base"]["iuse"] == manifest["base"]["use"] == []
    # Repair missing files, missing entries and versions that disagree with the lock.
    (project / "ttrek-venv/local/share/base.txt").unlink()
    assert built("install", "-y") == ["base@1.0.0", "middle@1.0.0", "top@1.0.0"]
    manifest = json.loads(manifest_path.read_text())
    del manifest["unrelated"]
    (project / "ttrek-venv/local/share/unrelated.txt").unlink()
    manifest_path.write_text(json.dumps(manifest))
    assert built("install", "-y") == ["unrelated@1.0.0"]
    assert json.loads(manifest_path.read_text())["unrelated"]["files"] == ["share/unrelated.txt"]
    manifest = json.loads(manifest_path.read_text())
    manifest["base"]["version"] = "0.9.0"
    manifest_path.write_text(json.dumps(manifest))
    assert built("install", "-y") == ["base@1.0.0", "middle@1.0.0", "top@1.0.0"]
    # Older manifests have no build flags; rebuild once to populate them.
    manifest = json.loads(manifest_path.read_text())
    del manifest["unrelated"]["iuse"]
    manifest_path.write_text(json.dumps(manifest))
    assert built("install", "-y") == ["unrelated@1.0.0"]
    assert built("install", "-y") == []

    # Internal history is retained but ordinary Git discovery cannot see it.
    venv = project / "ttrek-venv"
    gitdir = venv / ".ttrek-git"
    def git(*args):
        return subprocess.run(["git", "--git-dir", str(gitdir), *args], env=env,
                              text=True, capture_output=True, check=True).stdout.strip()
    assert gitdir.is_dir() and not (venv / ".git").exists()
    assert not any(path.startswith(".ttrek-git/") for path in git("ls-files").splitlines())
    assert subprocess.run(["git", "-C", str(venv), "rev-parse", "--show-toplevel"],
                          env=env, capture_output=True).returncode != 0
    head = git("rev-parse", "HEAD")
    gitdir.rename(venv / ".git")
    assert built("install", "-y", "top") == []
    assert gitdir.is_dir() and not (venv / ".git").exists()
    assert git("rev-parse", "HEAD") == head
    # Recovery from an interrupted install still uses the renamed history.
    (venv / "local/share/base.txt").write_text("broken")
    manifest_path.write_text("{}")
    (venv / "orphan.txt").write_text("partial install")
    (venv / "orphan-dir").mkdir()
    (venv / "orphan-dir/file").write_text("partial install")
    (venv / ".dirty").touch()
    recovered = run("install", "-y", "top")
    assert "Restored package environment to its last working state." in recovered.stdout
    assert "Saved package environment." in recovered.stdout
    assert not (venv / "orphan.txt").exists() and not (venv / "orphan-dir").exists()
    assert (venv / "local/share/base.txt").read_text() == "1.0.0"
    assert gitdir.is_dir()
    (venv / "debug-orphan.txt").touch()
    (venv / "debug-orphan-dir").mkdir()
    (venv / "debug-orphan-dir/file").touch()
    (venv / ".dirty").touch()
    recovered = run("install", "-y", "top", process_env=debug_env)
    assert "Hard reset to HEAD successful." in recovered.stdout
    assert "deleting file:" in recovered.stderr and "deleting directory:" in recovered.stderr

    recipe("base", graph["base"], "1.1.0")
    recipe("unrelated", graph["unrelated"], "1.1.0")
    shutil.rmtree(cloned / "ttrek-venv")
    run("install", "-y", cwd=cloned)
    assert (cloned / "ttrek-venv/local/share/base.txt").read_text() == "1.0.0"
    assert (cloned / "ttrek-venv/local/share/unrelated.txt").read_text() == "1.0.0"
    assert "unrelated@1.1.0" in run("pretend", "-strategy", "latest").stdout
    assert built("update", "-y", "base") == [
        "base@1.1.0", "middle@1.0.0", "top@1.0.0"]
    assert (project / "ttrek-venv/local/share/unrelated.txt").read_text() == "1.0.0"
    # A stale lock may select an already installed version: refresh it without rebuilding.
    lock_path = project / "ttrek-lock.json"
    locked = json.loads(lock_path.read_text())
    locked["packages"]["base"]["version"] = "1.0.0"
    lock_path.write_text(json.dumps(locked))
    assert built("update", "-y", "base") == []
    assert json.loads(lock_path.read_text())["packages"]["base"]["version"] == "1.1.0"
    assert built("install", "-y", "-save-dev", "devtool") == ["devtool@1.0.0"]
    assert built("install", "-y", "-D", "top") == []
    spec = json.loads(spec_path.read_text())
    assert built("install", "-y", "-save-dev") == []
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

    # Duplicate names are rejected consistently, including differing constraints.
    duplicate = root / "duplicate"
    duplicate.mkdir()
    invalid = {"dependencies": {"base": "1.0.0"}, "devDependencies": {"base": "1.1.0"}}
    invalid_path = duplicate / "ttrek.json"
    invalid_path.write_text(json.dumps(invalid))
    for command in ("install", "update", "pretend", "gen-script"):
        for packages in ((), ("base",)):
            failed = run(command, "-registry", str(registry), *packages, cwd=duplicate, ok=False)
            assert "base is listed in both dependencies and devDependencies" in failed.stderr
            assert json.loads(invalid_path.read_text()) == invalid

    # Updates respect saved ranges, and explicit overrides only change that target.
    recipe("unrelated", graph["unrelated"], "2.0.0")
    assert built("update", "-y", "unrelated") == []
    assert built("update", "-y", "unrelated@2.0.0") == ["unrelated@2.0.0"]
    assert json.loads(spec_path.read_text())["dependencies"]["unrelated"] == "2.0.0"
    assert "invalid strategy" in run("update", "-strategy", "invalid", ok=False).stderr
    assert "unknown update option" in run("update", "-bogus", ok=False).stderr

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
    (deep / "ttrek-venv/local").mkdir(parents=True)
    installed = {name: dict(package, files=[], iuse=[], use=[]) for name, package in packages.items()}
    (deep / "ttrek-venv/ttrek-manifest.json").write_text(json.dumps(installed))
    plan = run("pretend", "-force", "chain0", cwd=deep).stdout
    assert plan.count("reverse dependency") == 12, plan
    for index in range(13):
        assert f"chain{index}@1.0.0" in plan, plan

    assert "invalid strategy" in run("install", "-strategy", "invalid", ok=False).stderr
    assert "unknown install option: --save-dev" in run("install", "--save-dev", "devtool", ok=False).stderr
    for flag in ("-save-dev", "-D", "-force", "-strategy", "-fail-verbose", "-y"):
        assert flag in run("help", "install").stdout

print("Install options passed: dev dependencies, targeted rebuilds/updates, ordering, fresh installs, and deep reverse dependencies")
