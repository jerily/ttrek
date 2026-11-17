"""Installation settings must not overwrite project or installed-build metadata."""
import copy
import fcntl
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="ttrek-use-flags-") as temporary:
    root = Path(temporary)
    project = root / "project"
    project.mkdir()
    home = root / "home"
    home.mkdir()
    env = dict(os.environ, HOME=str(home), TTREK_NO_UPDATE_CHECK="1", TTREK_TELEMETRY="0")
    env.pop("TTREK_DEBUG", None)
    registry = root / "registry"
    spec_file = project / "ttrek.json"
    spec_file.write_text(json.dumps({"name": "demo", "version": "1.0.0", "dependencies": {},
                                     "useFlags": ["+ignored"]}))
    manifest = project / "ttrek-venv/ttrek-manifest.json"
    user_manifest = home / ".ttrek/ttrek-venv/ttrek-manifest.json"

    def run(*args, ok=True, cwd=project):
        result = subprocess.run([binary, *args], cwd=cwd, env=env, capture_output=True,
                                text=True, timeout=30)
        assert result.returncode >= 0, (args, result.stderr)
        assert (result.returncode == 0) == ok, (args, result.stdout, result.stderr)
        return result

    def flags(path=manifest):
        return set(json.loads(path.read_text())["useFlags"])

    # Reading never imports the project field. All CLI edits preserve the spec.
    original_spec = spec_file.read_bytes()
    assert run("use-flags", "ls").stdout == ""
    assert not manifest.exists()
    run("use-flags", "set", "+static", "-debug", "+static")
    assert flags() == {"+static", "-debug"}
    assert len(json.loads(manifest.read_text())["useFlags"]) == 2
    run("use-flags", "add", "-static", "+threads")
    assert flags() == {"-static", "-debug", "+threads"}
    run("use-flags", "del", "+static")
    assert flags() == {"-debug", "+threads"}
    run("use-flags", "set")
    assert flags() == set()

    # Parsed mode options must not become flags or discard the next argument.
    run("use-flags", "set", "-u", "+static")
    assert flags(user_manifest) == {"+static"} and flags() == set()
    run("use-flags", "add", "+debug", "-u")
    assert flags(user_manifest) == {"+static", "+debug"}
    run("use-flags", "add", "--", "-u", "-g")
    assert flags() == {"-u", "-g"}
    run("use-flags", "del", "-u", "+debug")
    assert flags(user_manifest) == {"+static"}
    assert set(run("use-flags", "ls", "-u").stdout.split()) == {"+static"}

    other = root / "other"
    other.mkdir()
    (other / "ttrek.json").write_text('{"dependencies": {}}')
    assert run("use-flags", "ls", cwd=other).stdout == ""
    assert spec_file.read_bytes() == original_spec

    before = manifest.read_bytes()
    for args in (("set", "+x", "-x"), ("add", "bare"), ("set", "+"),
                 ("set", "+two words"), ("del", "bad"), ("add",), ("del",),
                 ("ls", "+extra"), ("unknown",), ("set", "-u", "-g", "+x"), ("set", "-")):
        run("use-flags", *args, ok=False)
        assert manifest.read_bytes() == before
    long_flag = "+" + "x" * 512
    run("use-flags", "add", long_flag)
    assert long_flag in flags()
    run("use-flags", "del", long_flag)

    # Lock contention cannot overwrite the installation; writes are atomic.
    before = manifest.read_bytes()
    with (manifest.parent / ".lock").open("w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        run("use-flags", "set", "+locked", ok=False)
    assert manifest.read_bytes() == before
    manifest.chmod(0o640)
    run("use-flags", "add", "+permissions")
    assert manifest.stat().st_mode & 0o777 == 0o640
    if os.geteuid() != 0:
        before = manifest.read_bytes()
        manifest.parent.chmod(0o500)
        try:
            run("use-flags", "set", "+cannot-write", ok=False)
            assert manifest.read_bytes() == before
        finally:
            manifest.parent.chmod(0o700)
    saved = manifest.with_suffix(".saved")
    manifest.rename(saved)
    manifest.mkdir()
    run("use-flags", "set", "+cannot-write", ok=False)
    assert manifest.is_dir() and not list(manifest.parent.glob("*.tmp.*"))
    manifest.rmdir()
    saved.rename(manifest)

    # Malformed settings fail consistently; set can replace a bad flag array.
    for invalid in ("+static", {}, [42], ["+x", "-x"], ["+"]):
        manifest.write_text(json.dumps({"useFlags": invalid}))
        before = manifest.read_bytes()
        for command in (("use-flags", "ls"), ("use-flags", "add", "+ok"),
                        ("use-flags", "del", "+x"), ("pretend",), ("gen-script",)):
            run(*command, ok=False)
            assert manifest.read_bytes() == before
    run("use-flags", "set", "+static", "-debug")

    # A tiny recipe exposes several conditions without needing a compiler.
    def recipe(name, dependencies, steps):
        directory = registry / name / "1.0.0"
        directory.mkdir(parents=True)
        path = directory / "ttrek.json"
        value = {"dependencies": dependencies, "build": {"default": steps}}
        path.write_text(json.dumps(value))
        return path, value

    recipe("helper", {}, [])
    package_file, package = recipe("base", {"helper": {"version": "1.0.0", "if": "+static -debug"}},
                                  [{"cmd": "env_variable", "op": "set", "name": "FLAG_MARKER",
                                    "value": "STATIC_ENABLED", "if": "+static"}])
    (project / "ttrek-lock.json").write_text(json.dumps({"registry": str(registry)}))
    run("install", "-u", "-y", "-registry", str(registry), "base@1.0.0")
    assert json.loads(user_manifest.read_text())["base"]["use"] == ["-debug", "+static"]
    user_spec = json.loads((home / ".ttrek/ttrek.json").read_text())
    assert not {"useFlags", "iuse", "use"} & user_spec.keys()
    run("install", "-y", "-registry", str(registry), "base@1.0.0")
    installed = json.loads(manifest.read_text())
    assert installed["base"]["iuse"] == ["debug", "static"]
    assert set(installed["base"]["use"]) == {"+static", "-debug"}
    for invalid_iuse in ("local", [42], ["+static"], ["-debug"], ["+static -debug"]):
        invalid = copy.deepcopy(installed)
        invalid["base"]["iuse"] = invalid_iuse
        manifest.write_text(json.dumps(invalid))
        assert "iuse" in run("pretend", ok=False).stderr
    manifest.write_text(json.dumps(installed))
    assert "STATIC_ENABLED" in run("gen-script").stdout
    for key in ("iuse", "use"):
        assert key not in json.loads(spec_file.read_text())
        assert key not in json.loads(spec_file.read_text())["dependencies"]
    snapshot = copy.deepcopy(installed["base"])
    run("use-flags", "add", "-static")
    assert json.loads(manifest.read_text())["base"] == snapshot
    assert "USE flags changed" in run("pretend").stdout
    assert "STATIC_ENABLED" not in run("gen-script").stdout
    assert "STATIC_ENABLED" in run("gen-script", "-static").stdout
    assert flags() == {"-static", "-debug"}

    # Failure recovery restores old packages but retains newly requested flags.
    broken = copy.deepcopy(package)
    broken["build"]["default"].append({"cmd": "unknown-command"})
    package_file.write_text(json.dumps(broken))
    run("install", "-y", ok=False)
    run("use-flags", "add", "+retry")
    package_file.write_text(json.dumps(package))
    run("install", "-y")
    assert flags() == {"-static", "-debug", "+retry"}
    assert json.loads(manifest.read_text())["base"]["use"] == ["-debug", "-static"]
    assert "USE flags changed" not in run("pretend").stdout
    run("update", "-y")
    assert flags() == {"-static", "-debug", "+retry"}
    run("install", "-y", "useFlags@1.0.0", ok=False)
    assert flags() == {"-static", "-debug", "+retry"}
    run("uninstall", "-y", "useFlags", ok=False)
    assert flags() == {"-static", "-debug", "+retry"}
    run("uninstall", "-y", "base")
    assert flags() == {"-static", "-debug", "+retry"}

    # Root and transitive conditions use the same default-off semantics. A stale
    # lock must not turn disabled packages back into roots on install or update.
    graph = root / "graph"
    graph.mkdir()
    graph_spec = {"name": "graph", "iuse": ["local"], "dependencies": {
        "stack": "1.0.0", "pg-client": {"version": "1.0.0", "if": "-local"}},
        "devDependencies": {"mock": {"version": "1.0.0", "if": "+local -debug"}}}
    graph_file = graph / "ttrek.json"
    graph_file.write_text(json.dumps(graph_spec))
    graph_manifest = graph / "ttrek-venv/ttrek-manifest.json"
    graph_lock = graph / "ttrek-lock.json"
    graph_lock.write_text(json.dumps({"registry": str(registry)}))
    for name in ("pg-client", "postgres", "sqlite", "mock"):
        recipe(name, {}, [])
    stack_file, stack = recipe("stack", {
        "postgres": {"version": "1.0.0", "if": "-local"},
        "sqlite": {"version": "1.0.0", "if": "+local"}}, [
        {"cmd": "env_variable", "op": "set", "name": "DB_CHOICE", "value": "DEFAULT_POSTGRES", "if": "-local -debug"},
        {"cmd": "env_variable", "op": "set", "name": "DB_CHOICE", "value": "LOCAL_SQLITE", "if": "+local\t-debug"}])
    stack["iuse"] = ["local", "unused"]
    stack_file.write_text(json.dumps(stack))

    def selected():
        return set(json.loads(graph_lock.read_text())["packages"])

    run("install", "-y", cwd=graph)
    assert selected() == {"stack", "pg-client", "postgres"}
    record = json.loads(graph_manifest.read_text())["stack"]
    assert record["iuse"] == ["debug", "local", "unused"]
    assert record["use"] == ["-debug", "-local", "-unused"]
    assert record["requires"] == {"postgres": "1.0.0"}
    assert "DEFAULT_POSTGRES" in run("gen-script", cwd=graph).stdout
    run("use-flags", "add", "-local", cwd=graph)
    assert "Nothing to install" in run("pretend", cwd=graph).stdout
    run("use-flags", "add", "+local", cwd=graph)
    preview = run("pretend", cwd=graph).stdout
    assert "USE flags changed" in preview and "postgres@" not in preview and "pg-client@" not in preview
    generated = run("gen-script", "-strategy", "locked", cwd=graph).stdout
    assert "LOCAL_SQLITE" in generated and "DEFAULT_POSTGRES" not in generated
    run("install", "-y", cwd=graph)
    assert selected() == {"stack", "sqlite", "mock"}
    local_record = json.loads(graph_manifest.read_text())["stack"]
    assert local_record["requires"] == {"sqlite": "1.0.0"}
    assert "+local" in local_record["use"]
    assert json.loads(graph_file.read_text()) == graph_spec
    assert set(json.loads(graph_lock.read_text())["dependencies"]) == {"stack", "mock"}
    run("update", "-y", cwd=graph)
    assert selected() == {"stack", "sqlite", "mock"}
    assert json.loads(graph_file.read_text()) == graph_spec
    run("use-flags", "del", "+local", cwd=graph)
    run("install", "-y", cwd=graph)
    assert selected() == {"stack", "pg-client", "postgres"}
    # Inactive installed files/records are retained for an explicit uninstall.
    assert "mock" in json.loads(graph_manifest.read_text())
    assert json.loads(graph_file.read_text()) == graph_spec
    # Even an empty selected closure must clear stale lock roots/packages.
    graph_spec["dependencies"] = {}
    graph_file.write_text(json.dumps(graph_spec))
    run("install", "-y", cwd=graph)
    assert selected() == set() and json.loads(graph_lock.read_text())["dependencies"] == {}
    for invalid in ("local", " ", 42):
        graph_spec["devDependencies"]["mock"]["if"] = invalid
        graph_file.write_text(json.dumps(graph_spec))
        run("pretend", cwd=graph, ok=False)
        run("gen-script", cwd=graph, ok=False)

    graph_spec["devDependencies"]["mock"]["if"] = "-local"
    graph_file.write_text(json.dumps(graph_spec))
    damaged = json.loads(graph_manifest.read_text())
    damaged["stack"]["iuse"] = ["+local"]
    graph_manifest.write_text(json.dumps(damaged))
    assert "iuse" in run("install", "-y", cwd=graph, ok=False).stderr

print("USE flags: installation isolation, CLI validation, iuse arrays, rebuilds and recovery passed")
