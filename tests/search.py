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
    env = dict(os.environ, HOME=str(home), ENV="LOCAL", http_proxy="http://127.0.0.1:9",
               no_proxy="", NO_PROXY="")

    def run(*args, ok=True, directory=cwd):
        result = subprocess.run([BINARY, *args], cwd=directory, env=env,
                                capture_output=True, text=True, timeout=5)
        assert (result.returncode == 0) == ok, (args, result.stdout, result.stderr)
        return result

    def recipe(registry, name, version="1.0.0", description=None, tags=None):
        path = registry / name / version
        path.mkdir(parents=True, exist_ok=True)
        data = {} if description is None else {"description": description}
        if tags is not None:
            data["tags"] = tags
        (path / "ttrek.json").write_text(json.dumps(data))

    assert "search" in run("help").stdout
    assert "Levenshtein" in run("help", "search").stdout
    assert "Could not read registry" in run("search", "tcl", ok=False).stderr
    assert not list(home.iterdir()) and not list(cwd.iterdir())
    for args in (("search",), ("search", "a", "b"), ("search", ""),
                 ("search", "../x"), ("search", "a" * 256), ("search", "   "),
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
    assert run("search", "tcl").stdout.splitlines() == ["tcl", "tclx", "tcl-tk"]
    assert run("search", "abc").stdout.splitlines() == ["abc", "a+c", "a-c", "a.c", "a_c"]
    assert run("search", "TCL").stdout == run("search", "tcl").stdout
    assert run("search", "unknown").stdout == ""
    assert not (home / ".ttrek").exists() and not list(cwd.iterdir())

    def names(query):
        return [line.split("\t", 1)[0] for line in run("search", query).stdout.splitlines()]

    for name in ("web", "webserver", "atweb", "zzwebzzz", "b", "wec", "encryption"):
        recipe(registry, name)
    recipe(registry, "twebserver", description="A Tcl web server.")
    recipe(registry, "twebserver", "2.0.0", "Compression utilities.")
    recipe(registry, "portal", description="Embedded Web platform.")
    recipe(registry, "doc-server", description="Fast web server.")
    recipe(registry, "doc-encryption", description="Encryption toolkit.")
    recipe(registry, "literal-typo", description="Literal encrytion phrase.")
    assert names("web") == ["web", "webserver", "atweb", "zzwebzzz", "twebserver", "doc-server", "portal", "wec"]
    assert names("  WEB  ") == names("web")
    assert "b" not in names("web")
    assert names("web server")[:2] == ["doc-server", "twebserver"]
    assert names("encrytion")[:3] == ["literal-typo", "encryption", "doc-encryption"]
    assert run("search", "compression").stdout == "twebserver\tCompression utilities.\n"
    assert "twebserver\tA Tcl web server.\n" in run("search", "web").stdout
    assert names("b")[0] == "b"

    for name in ("json", "json-tools", "zzjsonzz", "jsox"):
        recipe(registry, name)
    recipe(registry, "described", description="JSON data formats.")
    recipe(registry, "tdom", description="DOM processing.", tags=["JSON", "xml", "JSON"])
    recipe(registry, "tdom", "2.0.0", tags=["sqlite"])
    recipe(registry, "desc-suggestion", description="A jsom parser.")
    recipe(registry, "tag-suggestion", tags=["jsom"])
    recipe(registry, "split-tags", tags=["relational", "storage"])
    recipe(registry, "one-tag", tags=["relational storage"])
    assert names("json") == ["json", "json-tools", "zzjsonzz", "described", "tdom",
                             "jsox", "desc-suggestion", "tag-suggestion"]
    assert names(" JSON ") == names("json")
    assert names("sqlite")[0] == "tdom"  # Union of tags across versions, ahead of typo suggestions.
    assert names("relational storage") == ["one-tag"]
    assert run("search", "xml").stdout == "tdom\tDOM processing.\n"
    assert "tdom" in names("sq")  # Short queries match tags literally.

    bad = registry / "bad/1.0.0/ttrek.json"
    for description in (42, None, {}, "bad\nline", "a" * 4097):
        bad.parent.mkdir(parents=True, exist_ok=True)
        bad.write_text(json.dumps({"description": description}))
        assert "description must be" in run("search", "web", ok=False).stderr
    for tags in (None, "json", {}, [42], [""], ["bad\nline"], ["x" * 256]):
        bad.write_text(json.dumps({"tags": tags}))
        assert "tags must" in run("search", "json", ok=False).stderr
    bad.write_text('{')
    assert "Invalid JSON" in run("search", "web", ok=False).stderr
    bad.unlink()

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
        assert [line.split("\t")[0] for line in run(*args, directory=nested).stdout.splitlines()] == ["tcl", "tclx", "tcl-tk", "twebserver"]
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

print("Name/description/tag ranking, trigram suggestions, metadata, registry selection and no-write checks passed")
