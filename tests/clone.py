"""Clone source archives without building, resolving dependencies or changing state."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import threading
from urllib.parse import urlsplit

BINARY = Path(sys.argv[1]).resolve()

with tempfile.TemporaryDirectory(prefix="ttrek clone ") as temporary:
    root = Path(temporary).resolve()
    home, work, registry = root / "home", root / "work", root / "registry"
    for directory in (home, work, registry):
        directory.mkdir()
    env = dict(os.environ, HOME=str(home), TTREK_NO_UPDATE_CHECK="1",
               http_proxy="http://127.0.0.1:9", https_proxy="http://127.0.0.1:9",
               ALL_PROXY="", NO_PROXY="127.0.0.1", no_proxy="127.0.0.1")
    env.pop("TTREK_DEBUG", None)

    def run(*args, ok=True, cwd=work):
        result = subprocess.run([BINARY, *args], cwd=cwd, env=env, capture_output=True,
                                text=True, timeout=20)
        assert (result.returncode == 0) == ok, (args, result.stdout, result.stderr)
        assert not list(root.rglob(".ttrek-clone-*"))
        return result

    def make_recipe(name, version, steps=None):
        archive = root / f"{name}-{version}.tar.gz"
        with tarfile.open(archive, "w:gz") as output:
            wrapper = tarfile.TarInfo("source/")
            wrapper.type = tarfile.DIRTYPE
            output.addfile(wrapper)
            for filename, data in (("version.txt", version.encode()), ("ttrek.json", b'{"dependencies":{}}')):
                entry = tarfile.TarInfo("source/" + filename)
                entry.size = len(data)
                entry.mtime = 1700000000
                entry.mode = 0o755
                output.addfile(entry, io.BytesIO(data))
        if steps is None:
            steps = [{"cmd": "download", "url": archive.as_uri()}, {"cmd": "unpack"},
                     {"cmd": "run", "command": "exit 99"}]
        spec = {"name": name, "version": version, "dependencies": {"does-not-exist": "99.0.0"},
                "build": {"default": steps}, "scripts": {"preInstall": "exit 98"}}
        path = registry / name / version / "ttrek.json"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(spec))
        return path, archive

    for version in ("1.0.0", "1.9.0", "1.10.0", "2.0.0-beta.1", "2.0.0"):
        make_recipe("demo", version)
    source_args = ["-registry", str(registry)]
    run("clone", *source_args, "demo")
    # Retain install's existing prerelease ordering and range policy.
    assert (work / "demo/version.txt").read_text() == "2.0.0-beta.1"
    assert sorted(p.name for p in (work / "demo").iterdir()) == ["ttrek.json", "version.txt"]
    assert (work / "demo/version.txt").stat().st_mtime == 1700000000
    assert (work / "demo/version.txt").stat().st_mode & 0o111
    for requirement, expected in (("1.0.0", "1.0.0"), ("^1.0.0", "1.10.0"),
                                  (">=1.9.0,<2.0.0", "1.10.0"),
                                  ("2.0.0-beta.1", "2.0.0-beta.1")):
        destination = root / ("destination " + str(len(list(root.iterdir()))))
        run(*source_args, "clone", f"demo@{requirement}", "-outdir", str(destination))
        assert (destination / "version.txt").read_text() == expected
    empty = root / "empty"
    empty.mkdir()
    inode = empty.stat().st_ino
    run("clone", *source_args, "demo", "-outdir", ".", cwd=empty)
    assert empty.stat().st_ino == inode and (empty / "version.txt").exists()
    nested = root / "new parent/source"
    run("clone", *source_args, "demo@1.0.0", "-outdir", str(nested))
    assert (nested / "version.txt").read_text() == "1.0.0"

    for args in ((), ("demo", "demo"), ("demo", "-outdir"), ("demo", "-outdir", ""),
                 ("../demo",), ("demo@invalid",), ("demo@>=9.0.0",), ("missing",)):
        run("clone", *source_args, *args, ok=False)
    assert "No matching version" in run("clone", *source_args, "demo@>=2.0.0,<1.0.0",
                                         "-outdir", str(root / "impossible"), ok=False).stderr
    for constraint, message in (("invalid", "parse version"), (">=9.0.0", "No matching version")):
        assert message in run("clone", *source_args, "demo@" + constraint,
                              "-outdir", str(root / "invalid"), ok=False).stderr
    assert not (root / "invalid").exists()
    run("clone", *source_args, "demo", ok=False)  # Existing content is preserved.
    assert (work / "demo/version.txt").read_text() == "2.0.0-beta.1"
    symlink = work / "link"
    symlink.symlink_to(root / "absent", target_is_directory=True)
    run("clone", *source_args, "demo", "-outdir", str(symlink), ok=False)
    assert symlink.is_symlink() and not (root / "absent").exists()
    make_recipe("meta", "1.0.0", [])
    assert "no source archive" in run("clone", *source_args, "meta", ok=False).stderr
    make_recipe("git-only", "1.0.0", [{"cmd": "git", "url": "https://example.invalid/repo"}])
    run("clone", *source_args, "git-only", ok=False)

    bad_spec, bad_archive = make_recipe("broken", "1.0.0")
    bad_archive.write_bytes(b"not an archive")
    run("clone", *source_args, "broken", ok=False)
    assert not (work / "broken").exists()
    with tarfile.open(bad_archive, "w:gz") as output:
        output.addfile(tarfile.TarInfo("source/partial.txt"))
        entry = tarfile.TarInfo("source/../../escape")
        output.addfile(entry)
    assert "unsafe" in run("clone", *source_args, "broken", ok=False).stderr
    assert not (work / "broken").exists() and not (root / "escape").exists()
    empty_failure = root / "empty-failure"
    empty_failure.mkdir()
    run("clone", *source_args, "broken", "-outdir", str(empty_failure), ok=False)
    assert not list(empty_failure.iterdir())

    # Hard links are rebased into the destination; links cannot escape staging.
    _, links_archive = make_recipe("links", "1.0.0")
    with tarfile.open(links_archive, "w:gz") as output:
        entry = tarfile.TarInfo("source/original")
        entry.size = 4
        output.addfile(entry, io.BytesIO(b"data"))
        entry = tarfile.TarInfo("source/hardlink")
        entry.type, entry.linkname = tarfile.LNKTYPE, "source/original"
        output.addfile(entry)
        entry = tarfile.TarInfo("source/symlink")
        entry.type, entry.linkname = tarfile.SYMTYPE, "original"
        output.addfile(entry)
    run("clone", *source_args, "links")
    assert (work / "links/hardlink").read_bytes() == b"data"
    assert (work / "links/symlink").read_bytes() == b"data"
    for kind, target in ((tarfile.LNKTYPE, str(root / "outside-file")),
                         (tarfile.SYMTYPE, str(root))):
        with tarfile.open(bad_archive, "w:gz") as output:
            entry = tarfile.TarInfo("source/escape")
            entry.type, entry.linkname = kind, target
            output.addfile(entry)
            if kind == tarfile.SYMTYPE:
                output.addfile(tarfile.TarInfo("source/escape/outside-file"))
        run("clone", *source_args, "broken", ok=False)
        assert not (root / "outside-file").exists() and not (work / "broken").exists()

    # Named primary archives and platform selection use the same recipe API.
    spec_path, archive = make_recipe("named", "1.0.0")
    spec = json.loads(spec_path.read_text())
    spec["build"]["default"] = []
    platform = ".".join((os.uname().sysname, os.uname().machine)).lower()
    spec["build"][platform] = [{"cmd": "download", "url": archive.as_uri(), "filename": "primary.tgz"},
                               {"cmd": "unpack", "filename": "primary.tgz"}]
    spec_path.write_text(json.dumps(spec))
    run("clone", *source_args, "named")

    # Saved registry selection from a project subdirectory is read-only.
    (work / "ttrek.json").write_text("{}")
    lock = work / "ttrek-lock.json"
    lock.write_text(json.dumps({"registry": "../registry"}))
    before = lock.read_bytes()
    child = work / "child"
    child.mkdir()
    run("clone", "demo@1.0.0", cwd=child)
    assert lock.read_bytes() == before and not (work / "ttrek-venv").exists()
    assert not (home / ".ttrek").exists()
    public = home / ".local/ttrek-registry"
    (public / ".git").mkdir(parents=True)
    (public / ".git/ttrek-last-sync").touch()
    (public / "demo").symlink_to(registry / "demo", target_is_directory=True)
    outside = root / "outside-project"
    outside.mkdir()
    run("clone", "demo@1.0.0", cwd=outside)
    assert (outside / "demo/version.txt").read_text() == "1.0.0"

    requests = []
    fallback_status = 200
    _, archive = make_recipe("http-demo", "1.0.0")
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def do_GET(self):
            requests.append(self.path)
            path = urlsplit(self.path).path
            if path.startswith("/download/"):
                if fallback_status != 200:
                    self.send_error(fallback_status)
                    return
                body = archive.read_bytes()
            elif path == "/registry/http-demo":
                body = json.dumps({"1.0.0": {"missing-dependency": "1.0.0"}}).encode()
            elif path.startswith("/registry/http-demo/1.0.0/"):
                body = json.dumps({"install_script": [
                    {"cmd": "download", "url": (root / "absent.tar.gz").as_uri()}, {"cmd": "unpack"}]}).encode()
            else:
                self.send_error(404)
                return
            self.send_response(200)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    address = f"http://127.0.0.1:{server.server_port}"
    env["TTREK_TEST_DOWNLOAD_ROOT"] = address + "/download"
    try:
        run("clone", "-registry", address + "/registry", "http-demo")
        assert (work / "http-demo/version.txt").read_text() == "1.0.0"
        assert any(path.startswith("/download/http-demo-1.0.0-") for path in requests)
        assert not any("missing-dependency" in path for path in requests)
        fallback_status = 503
        run("clone", "-registry", address + "/registry", "http-demo",
            "-outdir", str(root / "failed-download"), ok=False)
        assert not (root / "failed-download").exists()
    finally:
        server.shutdown()
        thread.join()
        server.server_close()

print("Clone versions, paths, archives, registry selection, mirror fallback and no-build checks passed")
