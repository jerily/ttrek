"""Exact-version language labels, persistence, updates and offline listing."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix="ttrek-languages-") as temporary:
    root = Path(temporary)
    project, registry, home = (root / name for name in ("project", "registry", "home"))
    for path in (project, registry, home):
        path.mkdir()
    env = dict(os.environ, HOME=str(home), TTREK_NO_UPDATE_CHECK="1", ENV="LOCAL",
               http_proxy="http://127.0.0.1:9", https_proxy="http://127.0.0.1:9")
    def run(*args):
        result = subprocess.run([binary, *args], cwd=project, env=env, text=True,
                                capture_output=True, timeout=20)
        assert result.returncode == 0, (args, result.stdout, result.stderr)
        return result.stdout

    def recipe(name, version, languages=None):
        directory = registry / name / version
        directory.mkdir(parents=True, exist_ok=True)
        data = {"name": name, "version": version, "dependencies": {}, "build": {"default": []}}
        if languages is not None:
            data["languages"] = languages
        (directory / "ttrek.json").write_text(json.dumps(data))

    recipe("native", "1.0.0", ["TCL", "C", "C"])
    recipe("legacy", "1.0.0")
    recipe("data", "1.0.0", [])
    run("init", "-y", "-registry", str(registry))
    assert "native@1.0.0 [C, TCL]" in run("pretend", "native")
    assert "native@1.0.0 [C, TCL]" in run("install", "-y", "native", "legacy", "data")
    assert set(run("ls").splitlines()) == {"native@1.0.0 [C, TCL]", "legacy@1.0.0", "data@1.0.0"}
    assert run("ls", "nat*") == "native@1.0.0 [C, TCL]\n"
    # Old lockfiles can read labels from their local registry without rewriting it.
    lock_path = project / "ttrek-lock.json"
    lock = json.loads(lock_path.read_text())
    del lock["packages"]["native"]["languages"]
    lock_path.write_text(json.dumps(lock))
    before = lock_path.read_bytes()
    assert run("ls", "native") == "native@1.0.0 [C, TCL]\n"
    assert lock_path.read_bytes() == before
    recipe("native", "1.1.0", ["C++", "TCL"])
    assert "native@1.1.0 [C++, TCL]" in run("pretend", "-strategy", "latest", "native")
    assert "native@1.1.0 [C++, TCL]" in run("update", "-y", "native")
    assert run("ls", "native") == "native@1.1.0 [C++, TCL]\n"
    # Metadata-only registry corrections must survive an update without a rebuild.
    recipe("native", "1.1.0", ["TCL"])
    assert "Nothing to install!" in run("update", "-y", "native")
    assert run("ls", "native") == "native@1.1.0 [TCL]\n"
    # The saved labels are usable without a reachable registry (including HTTP).
    lock_path = project / "ttrek-lock.json"
    lock = json.loads(lock_path.read_text())
    lock["registry"] = "http://127.0.0.1:9/registry"
    lock.pop("registries", None)
    lock_path.write_text(json.dumps(lock))
    assert run("ls", "native") == "native@1.1.0 [TCL]\n"
print("Install/update labels, metadata refresh and offline ls passed")
