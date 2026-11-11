"""Project file scripts preserve paths, preset arguments, hooks and source files."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

BINARY = Path(sys.argv[1]).resolve()
ROOT = Path(__file__).resolve().parents[1]

with tempfile.TemporaryDirectory(prefix="ttrek file scripts ' ") as temporary:
    root = Path(temporary).resolve()
    home, project, registry = root / "home", root / "project", root / "registry"
    for directory in (home, project, registry):
        directory.mkdir()
    env = dict(os.environ, HOME=str(home), SHELL="/bin/sh", TTREK_NO_UPDATE_CHECK="1",
               http_proxy="http://127.0.0.1:9", https_proxy="http://127.0.0.1:9",
               ALL_PROXY="http://127.0.0.1:9", NO_PROXY="", no_proxy="")

    def run(*args, cwd=project, ok=True):
        result = subprocess.run([BINARY, *args], cwd=cwd, env=env,
                                capture_output=True, text=True, timeout=10)
        assert (result.returncode == 0) == ok, (args, result.stdout, result.stderr)
        return result

    run("init", "-y", "-registry", str(registry))
    spec_path = project / "ttrek.json"
    scripts_dir = project / "script files"
    scripts_dir.mkdir()
    probe = scripts_dir / "probe.sh"
    body = ('#!/usr/bin/env bash\nset -eu\n'
            'printf "%s\\n" "$0" "${BASH_SOURCE[0]}" "$PWD" "$PROJECT_HOME"\n'
            'for arg do printf "[%s]\\n" "$arg"; done\n')
    probe.write_text(body)
    probe.chmod(0o755)
    presets = ["fixed", "", "two words", "café 世界", "'quoted'", "$(touch injected)", "-script"]
    extra = ["", "more words", "`touch injected`", "; touch injected", "*", "-u", "--"]
    spec = {"dependencies": {}, "scripts": {
        "probe": {"file": "script files/probe.sh", "args": presets},
        "inline": "printf inline", "no-args": {"file": "script files/probe.sh"}}}
    spec_path.write_text(json.dumps(spec))
    child = project / "nested"
    child.mkdir()
    before = spec_path.read_bytes()
    assert set(run("scripts", "ls").stdout.splitlines()) == set(spec["scripts"])
    assert run("scripts", "describe", "probe", cwd=child).stdout == body + "\n"
    expected = [str(probe), str(probe), str(project / "ttrek-venv/local"), str(project)]
    assert run("run", "-script", "probe", "--", *extra, cwd=child).stdout.splitlines() == (
        expected + [f"[{arg}]" for arg in presets + extra])
    assert run("run", "-script", "no-args").stdout.splitlines() == expected
    assert run("run", "-script", "inline").stdout == "inline"
    assert not (project / "ttrek-venv/local/injected").exists()
    assert spec_path.read_bytes() == before and probe.read_text() == body
    assert not list((project / "ttrek-venv/tmp").glob("script-*.sh"))

    # Editing an unrelated script must retain references instead of inlining them.
    run("scripts", "add", "temporary", "exit 0")
    assert json.loads(spec_path.read_text())["scripts"]["probe"] == spec["scripts"]["probe"]
    run("scripts", "del", "temporary")
    assert json.loads(spec_path.read_text()) == spec

    for invalid in (42, {}, {"file": ""}, {"file": "missing.sh"},
                    {"file": "script files"}, {"file": str(probe)}, {"file": "../outside.sh"},
                    {"file": "script files/probe.sh", "args": "bad"},
                    {"file": "script files/probe.sh", "args": [1]},
                    {"file": "script files/probe.sh", "args": None},
                    {"file": "script files/probe.sh", "unknown": True}):
        spec["scripts"]["invalid"] = invalid
        spec_path.write_text(json.dumps(spec))
        run("run", "-script", "invalid", ok=False)
        run("scripts", "describe", "invalid", ok=False)
        assert probe.read_text() == body
    del spec["scripts"]["invalid"]
    spec_path.write_text(json.dumps(spec))
    probe.chmod(0o644)
    assert "not executable" in run("run", "-script", "probe", ok=False).stderr
    assert run("scripts", "describe", "probe").stdout == body + "\n"
    probe.chmod(0o755)
    probe.write_text("#!/bin/sh\nexit 7\n")
    assert "7" in run("run", "-script", "probe", ok=False).stderr
    assert probe.read_text() == "#!/bin/sh\nexit 7\n"
    probe.write_bytes(b"#!/bin/sh\n#\0\n")
    assert "NUL" in run("run", "-script", "probe", ok=False).stderr

    hook = scripts_dir / "hook.sh"
    hook.write_text('#!/bin/sh\nprintf "%s\\n" "$@" > "$PROJECT_HOME/$1.marker"\n')
    hook.chmod(0o755)
    for name in ("preInstall", "postInstall"):
        spec["scripts"][name] = {"file": "script files/hook.sh", "args": [name, "two words"]}
    spec_path.write_text(json.dumps(spec))
    run("install", "-y")
    for name in ("preInstall", "postInstall"):
        assert (project / f"{name}.marker").read_text() == name + "\ntwo words\n"
    assert hook.exists() and json.loads(spec_path.read_text())["scripts"] == spec["scripts"]

    # The real release aliases still call the original bin scripts with the
    # same architecture/environment presets. Stub the scripts to avoid releases.
    aliases = {
        "prepare-linux-amd64-glibc": ("prepare-linux.sh", ["amd64", "glibc"]),
        "prepare-linux-arm64-glibc": ("prepare-linux.sh", ["arm64", "glibc"]),
        "prepare-linux-amd64-musl": ("prepare-linux.sh", ["amd64", "musl"]),
        "prepare-linux-arm64-musl": ("prepare-linux.sh", ["arm64", "musl"]),
        "test-linux-amd64": ("test-linux-release.sh", ["amd64"]),
        "test-linux-arm64": ("test-linux-release.sh", ["arm64"]),
        "prepare-macos-target-11.0": ("prepare-macos-target-11.0.sh", []),
        "prepare-macos-arm64": ("prepare-macos.sh", ["arm64"]),
        "prepare-macos-x86_64": ("prepare-macos.sh", ["x86_64"]),
        "prepare-macos-universal2": ("prepare-universal2.sh", []),
        "upload-local": ("upload.sh", ["local"]),
        "upload-prod": ("upload.sh", ["prod"]),
    }
    (project / "bin").mkdir()
    for filename, _ in aliases.values():
        stub = project / "bin" / filename
        stub.write_text(f"#!{sys.executable}\nimport json, sys\nprint(json.dumps(sys.argv))\n")
        stub.chmod(0o755)
    spec_path.write_text((ROOT / "ttrek.json").read_text())
    assert set(run("scripts", "ls").stdout.splitlines()) == set(aliases)
    for name, (filename, defaults) in aliases.items():
        output = run("run", "-script", name, "--", *extra, cwd=child).stdout
        assert json.loads(output) == [str(project / "bin" / filename), *defaults, *extra]
        assert (project / "bin" / filename).exists()

print("Project file scripts, preset arguments, hooks and release aliases passed")
