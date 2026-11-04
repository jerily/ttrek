#!/usr/bin/env python3
"""Offline search: distances, registry precedence, deduplication and no writes."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

BINARY = str(Path(sys.argv[1]).resolve())

with tempfile.TemporaryDirectory(prefix="ttrek-search-") as temporary:
    root = Path(temporary)
    home = root / "home"
    home.mkdir()
    cwd = root / "work"
    cwd.mkdir()
    env = dict(os.environ, HOME=str(home), ENV="LOCAL")

    def run(*args, ok=True, directory=cwd):
        result = subprocess.run([BINARY, *args], cwd=directory, env=env,
                                capture_output=True, text=True, timeout=5)
        assert (result.returncode == 0) == ok, (args, result.stdout, result.stderr)
        return result

    def recipe(registry, name, version="1.0.0"):
        path = registry / name / version
        path.mkdir(parents=True, exist_ok=True)
        # Search indexes names without parsing or executing recipes.
        (path / "ttrek.json").write_text('{}')

    assert "search" in run("help").stdout
    assert "Levenshtein" in run("help", "search").stdout
    assert "ttrek sync" in run("search", "tcl", ok=False).stderr
    assert not list(home.iterdir()) and not list(cwd.iterdir())
    for args in (("search",), ("search", "a", "b"), ("search", ""),
                 ("search", "../x"), ("search", "a" * 256), ("search", "a b"),
                 ("search", "é"), ("search", "-registry")):
        run(*args, ok=False)

    registry = home / ".local/ttrek-registry"
    for name in ("tcl", "tclx", "tk", "tcl-tk", "a-c", "a+c", "a.c", "a_c", "abc", "zzz"):
        recipe(registry, name)
    recipe(registry, "tcl", "2.0.0")
    recipe(registry, ".hidden")
    recipe(registry, "bad name")
    (registry / "tclz/1.0.0").mkdir(parents=True)  # No recipe.
    (registry / "tclq").write_text("not a package directory")
    assert run("search", "tcl").stdout.splitlines() == ["tcl", "tclx", "tk"]
    assert run("search", "abc").stdout.splitlines() == ["abc", "a+c", "a-c", "a.c", "a_c"]
    assert run("search", "TCL").stdout == ""
    assert run("search", "unknown").stdout == ""
    assert not (home / ".ttrek").exists() and not list(cwd.iterdir())

    other = root / "other registry [names]"
    recipe(other, "tcli")
    (cwd / "ttrek.json").write_text('{}')
    lock = cwd / "ttrek-lock.json"
    lock.write_text(json.dumps({"registry": "../other registry [names]"}))
    nested = cwd / "child"
    nested.mkdir()
    assert run("search", "tcl", directory=nested).stdout == "tcli\n"
    for args in (("search", "-registry", str(registry), "tcl"),
                 ("-registry", str(registry), "search", "tcl")):
        assert run(*args, directory=nested).stdout.splitlines() == ["tcl", "tclx", "tk"]
    assert not (cwd / "ttrek-venv").exists()
    lock.write_text('{"registry": "../absent"}')
    assert run("search", "tcl", ok=False).stdout == ""
    assert run("search", "-registry", str(other), "tcl").stdout == "tcli\n"
    lock.write_text('{"registry": 42}')
    assert "registry must be" in run("search", "tcl", ok=False).stderr
    lock.write_text('{} trailing')
    assert "Invalid JSON" in run("search", "tcl", ok=False).stderr
    empty = root / "empty"
    empty.mkdir()
    assert run("search", "-registry", str(empty), "tcl").stdout == ""

print("Offline recipe search, ordering, registry selection and no-write checks passed")
