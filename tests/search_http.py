"""HTTP search fallback, response validation and mixed registry precedence."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
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
    packages = {"shadow": {"1.0.0": {}}}

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def do_GET(self):
            requests.append(self.path)
            path = urlsplit(self.path).path
            if path.endswith("/search"):
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
        assert not list(home.iterdir()) and not list(work.iterdir())
        for query in ("web server", "a+b", "  TCL  "):
            run("search", "-registry", address + "/custom", query)
            request = urlsplit(requests[-1])
            assert request.path == "/custom/search"
            assert parse_qs(request.query) == {"q": [query.strip().lower()]}
        before = len(requests)
        for query in ("", "..", "a/b", "a" * 256, "é", "\n"):
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
        # Earlier HTTP metadata hides a local hit even when its search has no hit.
        assert run("search", "-registry", address + "/registry", "-registry", str(local), "tcl").stdout == (
            "tcl\tRemote package\ntcl-local\tLocal package\nlocal-owner\tTcl remote\n")
        # Earlier local metadata likewise hides the remote result.
        assert run("-registry", str(local), "search", "-registry", address + "/registry", "tcl").stdout == (
            "tcl\tRemote package\ntcl-local\tLocal package\nshadow\tTcl hidden locally\n")
        assert "Remote package" in run("search", "-registry", str(local), "-add-pubreg", "tcl").stdout
        assert any(url.startswith("http://localhost:8080/registry/search?") for url in requests)

        (work / "ttrek.json").write_text("{}")
        lock = work / "ttrek-lock.json"
        lock.write_text(json.dumps({"registries": [{"url": address + "/saved"}]}))
        before_lock = lock.read_bytes()
        child = work / "child"
        child.mkdir()
        run("search", "tcl", cwd=child)
        assert urlsplit(requests[-1]).path == "/saved/search"
        assert lock.read_bytes() == before_lock and not (work / "ttrek-venv").exists()
        assert not list(home.iterdir())
    finally:
        server.shutdown()
        thread.join()
        server.server_close()

print("HTTP search, default fallback, validation, precedence and no-write checks passed")
