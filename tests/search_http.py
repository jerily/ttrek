"""HTTP search fallback, response validation and mixed registry precedence."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
import platform
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
from urllib.parse import parse_qs, urlsplit

BINARY = Path(sys.argv[1]).resolve()

with tempfile.TemporaryDirectory(prefix="ttrek-search-http-") as temporary:
    root = Path(temporary)
    home, work = root / "home", root / "work"
    home.mkdir()
    work.mkdir()
    requests = []
    response = [{"name": "tcl", "description": "Tcl language", "match": "exact", "distance": 0}]
    status = 200
    endpoints = {}
    packages = {"shadow": {"1.0.0": {}}}

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def do_GET(self):
            requests.append(self.path)
            path = urlsplit(self.path).path
            if path in endpoints:
                body = endpoints[path]
                code = 200
            elif path.endswith("/search"):
                body = response
                code = status
            else:
                body = packages.get(path.rsplit("/", 1)[-1], {})
                code = 200 if body else 404
            payload = body.encode() if isinstance(body, str) else json.dumps(body).encode()
            self.send_response(code)
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)

    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    address = f"http://127.0.0.1:{server.server_port}"
    env = dict(os.environ, HOME=str(home), ENV="LOCAL", TTREK_NO_UPDATE_CHECK="1",
               http_proxy=address, https_proxy=address, no_proxy="", NO_PROXY="", ALL_PROXY="")

    def run(*args, ok=True, cwd=work):
        result = subprocess.run([BINARY, *args], cwd=cwd, env=env, capture_output=True,
                                text=True, timeout=10)
        assert (result.returncode == 0) == ok, (args, result.stdout, result.stderr)
        if not ok:
            assert result.stdout == "", result
        return result

    try:
        assert run("search", "tcl").stdout == "tcl\tTcl language\n"
        assert requests == ["http://localhost:8080/registry/search?q=tcl"]
        response[0]["languages"] = ["TCL", "C", "C"]
        assert run("search", "tcl").stdout == "tcl [C, TCL]\tTcl language\n"
        for languages in (None, "C", [42], ["C\nTCL"]):
            response[0]["languages"] = languages
            assert "languages" in run("search", "tcl", ok=False).stderr
        del response[0]["languages"]
        assert not list(home.iterdir()) and not list(work.iterdir())
        for query in ("web server", "a+b", "  TCL  "):
            run("search", "-registry", address + "/custom", query)
            request = urlsplit(requests[-1])
            assert request.path == "/custom/search"
            assert parse_qs(request.query) == {"q": [query.strip().lower()]}
        before = len(requests)
        for args in ((), ("one", "two"), ("-registry",)):
            run("search", *args, ok=False)
        for query in ("", "   ", ".", "..", "a/b", "a" * 256, "é", "\n"):
            run("search", query, ok=False)
        assert len(requests) == before

        for status, diagnostic in ((401, "authentication"), (403, "denied"),
                                   (404, "search endpoint"), (302, "redirected"), (500, "Could not read registry")):
            assert diagnostic in run("search", "tcl", ok=False).stderr
        status = 200
        for response in ("{", "[] trailing", {}, [42], [{"name": "../x"}],
                         [{"name": "tcl", "description": "bad\nline", "match": "exact"}],
                         [{"name": "tcl", "description": "", "match": "unknown"}],
                         [{"name": "tcl", "description": "", "match": "fuzzy", "distance": -1}]):
            assert "Invalid registry search" in run("search", "tcl", ok=False).stderr
        response = []
        assert run("search", "nothing").stdout == ""

        response = [
            {"name": "tag-suggestion", "description": "", "match": "description-fuzzy", "field": "tags", "distance": 1},
            {"name": "desc-suggestion", "description": "jsom parser", "match": "description-fuzzy", "distance": 1},
            {"name": "jsox", "description": "", "match": "fuzzy", "distance": 1},
            {"name": "tdom", "description": "DOM processing", "tags": ["json"], "match": "description", "field": "tags", "distance": 0},
            {"name": "described", "description": "JSON data", "match": "description", "distance": 0},
            {"name": "json", "description": "", "match": "exact", "distance": 0},
        ]
        assert [line.split("\t")[0] for line in run("search", "json").stdout.splitlines()] == [
            "json", "described", "tdom", "jsox", "desc-suggestion", "tag-suggestion"]
        for match, field in (("exact", "tags"), ("description", 42), ("description", "unknown")):
            response = [{"name": "tdom", "description": "", "match": match, "field": field}]
            assert "Invalid registry search field" in run("search", "json", ok=False).stderr

        local = root / "local"
        for name, description in (("shadow", "Tcl hidden locally"), ("tcl-local", "Local package"),
                                  ("local-owner", "No matching description")):
            recipe = local / name / "1.0.0/ttrek.json"
            recipe.parent.mkdir(parents=True)
            recipe.write_text(json.dumps({"description": description}))
        response = [{"name": "tcl", "description": "Remote package", "match": "exact", "distance": 0},
                    {"name": "local-owner", "description": "Tcl remote", "match": "description", "distance": 0}]
        expected = "tcl\tRemote package\nlocal-owner\tTcl remote\n"
        # Invalid explicit sources fail before contacting any website, even in mixed lists.
        before = len(requests)
        for path in (str(local), str(local / "missing"), "../registry", "."):
            for args in (("search", "-registry", path), ("-registry", path, "search"),
                         ("search", "-registry", address + "/custom", "-registry", path),
                         ("-registry", path, "search", "-add-pubreg")):
                assert "HTTP(S) registry API URL" in run(*args, "tcl", ok=False).stderr
        assert len(requests) == before
        status = 404
        assert "search endpoint" in run("search", "-registry", address + "/unsupported", "tcl", ok=False).stderr
        assert requests[-1] == address + "/unsupported/search?q=tcl"
        status = 200

        # A synced checkout has no effect on the default search website.
        stamp = home / ".local/ttrek-registry/.git/ttrek-last-sync"
        stamp.parent.mkdir(parents=True)
        stamp.touch()
        before = len(requests)
        assert run("search", "tcl").stdout == expected
        assert requests[before:] == ["http://localhost:8080/registry/search?q=tcl"]

        # The first website owns its packages even if only the second found a match.
        endpoints["/custom/search"] = []
        endpoints["/registry/search"] = [
            {"name": "shadow", "description": "", "match": "description"}, *response]
        assert run("search", "-registry", address + "/custom", "-add-pubreg", "tcl").stdout == expected

        (work / "ttrek.json").write_text("{}")
        lock = work / "ttrek-lock.json"
        lock.write_text(json.dumps({"registries": [{"directory": str(local)}, {"url": address + "/saved"}]}))
        before_lock = lock.read_bytes()
        child = work / "child"
        child.mkdir()
        run("search", "tcl", cwd=child)
        assert urlsplit(requests[-1]).path == "/saved/search"
        assert lock.read_bytes() == before_lock and not (work / "ttrek-venv").exists()
        lock.write_text(json.dumps({"registry": str(local / "missing")}))
        assert run("search", "tcl", cwd=child).stdout.startswith("tcl\tRemote package\n")
        assert requests[-1] == "http://localhost:8080/registry/search?q=tcl"
        assert not (work / "ttrek-venv").exists()

        # Language metadata has its own lightweight endpoint, so previewing a
        # plan does not request a build recipe. Older servers may return 404.
        project = root / "install-project"
        project.mkdir()
        packages["native"] = {"1.0.0": {}}
        endpoints["/registry/native/1.0.0/languages"] = ["C", "TCL"]
        build_path = f"/registry/native/1.0.0/{platform.system()}/{platform.machine()}"
        endpoints[build_path] = {"version": "1.0.0", "dependencies": {}, "iuse": [],
                                 "install_script": [], "scripts": {}, "languages": ["C", "TCL"]}
        run("init", "-y", "-registry", address + "/registry", cwd=project)
        before = len(requests)
        assert "native@1.0.0 [C, TCL]" in run("pretend", "native", cwd=project).stdout
        assert build_path not in requests[before:]
        assert "native@1.0.0 [C, TCL]" in run("install", "-y", "native", cwd=project).stdout
        before = len(requests)
        assert run("ls", "native", cwd=project).stdout == "native@1.0.0 [C, TCL]\n"
        assert len(requests) == before
        # An older server lacking both metadata fields remains installable.
        del endpoints["/registry/native/1.0.0/languages"]
        del endpoints[build_path]["languages"]
        assert "native@1.0.0" in run("install", "-y", "-force", "native", cwd=project).stdout
        assert run("ls", "native", cwd=project).stdout == "native@1.0.0\n"
    finally:
        server.shutdown()
        thread.join()
        server.server_close()

print("HTTP search, default fallback, validation, precedence and no-write checks passed")
