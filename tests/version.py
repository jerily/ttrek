"""Version information is offline and reports effective runtime/build settings."""
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

binary, fixture = (Path(arg).resolve() for arg in sys.argv[1:3])
debug_build = sys.argv[3] == "Debug"

with tempfile.TemporaryDirectory(prefix="ttrek-version-") as directory:
    root = Path(directory)
    home = root / "home"
    home.mkdir()
    env = dict(os.environ, HOME=str(home), http_proxy="http://127.0.0.1:9",
               https_proxy="http://127.0.0.1:9", ALL_PROXY="http://127.0.0.1:9")
    for name in ("TTREK_DEBUG", "TTREK_TELEMETRY", "TTREK_NO_UPDATE_CHECK"):
        env.pop(name, None)

    def run(*args, executable=binary, process_env=env, cwd=root, ok=True):
        result = subprocess.run([executable, *args], env=process_env, cwd=cwd,
                                capture_output=True, text=True, timeout=10)
        assert (result.returncode == 0) == ok, (args, result.stdout, result.stderr)
        assert "ttrek_Telemetry" not in result.stderr, result.stderr
        return result

    numeric = run("self-update", "-version").stdout.strip()
    assert re.fullmatch(r"\d+\.\d+\.\d+", numeric), numeric

    def version(*options, **kwargs):
        result = run(*options, "version", **kwargs)
        lines = result.stdout.splitlines()
        assert len(lines) == 5 and lines[0] == f"ttrek {numeric}", result.stdout
        fields = dict(line.split(": ", 1) for line in lines[1:])
        assert re.fullmatch(r"linux-(amd64|arm64)-(glibc|musl)|macos-(x86_64|arm64)|unknown",
                            fields["Platform"]), result.stdout
        return fields

    default = version()
    assert default["Telemetry"] == "on"
    assert default["Debug"] == "off (TTREK_DEBUG)"
    assert default["Debug build"] == ("on" if debug_build else "off")
    assert version(executable=fixture)["Debug build"] == "on"
    for value in ("1", "0", ""):
        assert version(process_env=dict(env, TTREK_DEBUG=value))["Debug"] == "on (TTREK_DEBUG)"
    for value in ("0", "false", "no", "off", "FALSE", "", "invalid"):
        assert version(process_env=dict(env, TTREK_TELEMETRY=value))["Telemetry"] == "off"
    for value in ("1", "true", "yes", "on", "TRUE"):
        assert version(process_env=dict(env, TTREK_TELEMETRY=value))["Telemetry"] == "on"
    for args in (("extra",), ("--bogus",)):
        result = run("version", *args, ok=False)
        assert not result.stdout and "Usage: ttrek version" in result.stderr
    assert list(root.iterdir()) == [home] and not list(home.iterdir())

    registry = root / "registry"
    registry.mkdir()
    for source in (str(registry), "https://example.invalid/registry"):
        assert version("-registry", source, process_env=dict(env, TTREK_TELEMETRY="1"))["Telemetry"] == "off"
    assert version("-add-pubreg")["Telemetry"] == "off"

    project = root / "project"
    nested = project / "nested"
    nested.mkdir(parents=True)
    (project / "ttrek.json").write_text("{}")
    lock = project / "ttrek-lock.json"
    for config in ({"registry": "../registry"},
                   {"registries": [{"url": "https://example.invalid/registry"}]}):
        lock.write_text(json.dumps(config))
        before = lock.read_bytes()
        assert version(cwd=nested)["Telemetry"] == "off"
        assert lock.read_bytes() == before
    lock.write_text("invalid JSON")
    assert version(cwd=nested)["Telemetry"].startswith("unknown")
    assert version(cwd=nested, process_env=dict(env, TTREK_TELEMETRY="0"))["Telemetry"] == "off"
    assert not list(nested.iterdir()) and not (project / "ttrek-venv").exists()

    # A synced public registry changes the default policy without network access.
    stamp = home / ".local/ttrek-registry/.git/ttrek-last-sync"
    stamp.parent.mkdir(parents=True)
    stamp.touch()
    assert version()["Telemetry"] == "off"
    assert not (home / ".ttrek").exists()

print("Version output, telemetry policy, debug flags and read-only behavior passed")
