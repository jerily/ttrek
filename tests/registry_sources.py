"""Ordered local and HTTP registries, with isolated projects and HTTP fixtures."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading

BINARY = Path(sys.argv[1]).resolve()

with tempfile.TemporaryDirectory(prefix="ttrek ordered registries ") as temporary:
    root = Path(temporary)
    home = root / "home"
    home.mkdir()
    project = root / "project"
    project.mkdir()
    local = root / "local registry"
    local.mkdir()
    env = dict(os.environ, HOME=str(home), TTREK_NO_UPDATE_CHECK="1", SHELL="/bin/sh",
               http_proxy="http://127.0.0.1:9", https_proxy="http://127.0.0.1:9",
               ALL_PROXY="http://127.0.0.1:9", NO_PROXY="127.0.0.1", no_proxy="127.0.0.1")

    def run(*args, ok=True, cwd=project):
        result = subprocess.run([BINARY, *args], cwd=cwd, env=env,
                                capture_output=True, text=True, timeout=30)
        assert (result.returncode == 0) == ok, (args, result.stdout, result.stderr)
        return result

    def recipe(directory, name, version="1.0.0", dependencies=None):
        path = directory / name / version / "ttrek.json"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps({"name": name, "version": version, "dependencies": dependencies or {},
                                    "description": name, "build": {"default": []}}))
        return path

    recipe(local, "local-only")
    recipe(local, "shadow", "3.0.0")
    metadata = {
        "first": {
            "app": {"1.0.0": {"shared": ">=1.0.0", "public-only": "1.0.0", "local-only": "1.0.0"}},
            "shared": {"1.0.0": {}},
            "shadow": {"1.0.0": {}},
            "missing-recipe": {"1.0.0": {}},
        },
        "second": {
            "shared": {"9.0.0": {}},
            "public-only": {"1.0.0": {}},
            "shadow": {"2.0.0": {}},
            "missing-recipe": {"1.0.0": {}},
        },
    }
    requests = []
    first_status = None
    first_body = None

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_POST(self):
            requests.append((self.command, self.path, dict(self.headers)))
            self.send_error(500, "Unexpected telemetry")

        def do_GET(self):
            requests.append((self.command, self.path, dict(self.headers)))
            parts = self.path.strip("/").split("/")
            source, name = (parts + ["", ""])[:2]
            status = 200
            body = None
            if source == "first" and first_status is not None:
                status, body = first_status, first_body
            elif name not in metadata.get(source, {}):
                status = 404
            elif len(parts) == 2:
                body = metadata[source][name]
            elif len(parts) == 5:
                version = parts[2]
                if version not in metadata[source][name] or (source == "first" and name == "missing-recipe"):
                    status = 404
                else:
                    body = {"version": version, "dependencies": metadata[source][name][version],
                            "install_script": [], "scripts": {"source": "printf " + source}}
            else:
                status = 404
            self.send_response(status)
            if 300 <= status < 400:
                self.send_header("Location", "/second/app")
            data = (json.dumps(body) if not isinstance(body, str) else body).encode()
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

    with ThreadingHTTPServer(("127.0.0.1", 0), Handler) as server:
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            base = f"http://127.0.0.1:{server.server_port}"
            first, second = base + "/first", base + "/second"
            options = ("-registry", first + "/", "-registry", second, "-registry", "../local registry")
            run("init", "-y", *options)
            lock_file = project / "ttrek-lock.json"
            spec_file = project / "ttrek.json"
            settings = {"registries": [{"url": first}, {"url": second}, {"directory": "../local registry"}]}
            assert json.loads(lock_file.read_text())["registries"] == settings["registries"]
            spec = json.loads(spec_file.read_text())
            spec["dependencies"] = {"app": "1.0.0"}
            spec_file.write_text(json.dumps(spec))

            plan = run("pretend").stdout
            for package in ("app", "shared", "public-only", "local-only"):
                assert package + "@1.0.0" in plan, plan
            assert not any(path.startswith("/second/shared") for _, path, _ in requests)
            generated = run("gen-script").stdout
            assert generated.startswith("#!")
            assert generated == run(*options, "gen-script").stdout
            assert generated == run("gen-script", *options).stdout
            assert generated == run("-registry", first, "gen-script", "-registry", second,
                                    "-registry", "../local registry").stdout
            nested = project / "nested"
            nested.mkdir()
            assert generated == run("gen-script", cwd=nested).stdout
            assert all(method == "GET" and "Authorization" not in headers and
                       "TTrek-Environment-Id" not in headers for method, _, headers in requests)
            assert not (project / "ttrek-venv/machine-id").exists()

            # Actual install/update load recipes from the source that owns each package.
            run("install", "-y")
            assert run("run", "-package", "app", "-script", "source").stdout == "first"
            assert run("run", "-package", "public-only", "-script", "source").stdout == "second"
            run("update", "-y")
            assert json.loads(lock_file.read_text())["registries"] == settings["registries"]

            def dependency(name, version="1.0.0"):
                spec["dependencies"] = {name: version}
                spec_file.write_text(json.dumps(spec))
                lock_file.write_text(json.dumps(settings))
                requests.clear()

            dependency("shadow", ">=2.0.0")
            run("pretend", ok=False)  # Existing package in source one shadows all later versions.
            assert not any(path.startswith("/second/shadow") for _, path, _ in requests)
            before = lock_file.read_bytes()
            assert "shadow@2.0.0" in run("pretend", "-registry", second).stdout
            assert lock_file.read_bytes() == before

            dependency("missing-recipe")
            failed = run("gen-script", ok=False)
            assert not failed.stdout and "was not found" in failed.stderr
            assert not any(path.startswith("/second/missing-recipe") for _, path, _ in requests)

            dependency("absent")
            failed = run("pretend", ok=False)
            assert failed.stderr.count("error:") == 1 and 'Package "absent" was not found' in failed.stderr
            assert all(source in failed.stderr for source in (first, second, str(local)))

            # Only a metadata 404 advances to another registry. All other failures stop.
            dependency("public-only")
            for status, body in ((401, "private failure"), (403, "private failure"), (500, "private failure"),
                                 (302, "private failure"), (200, "malformed JSON"), (200, []),
                                 (200, {"1.0.0": {"invalid": 42}})):
                first_status, first_body = status, body
                requests.clear()
                failed = run("gen-script", ok=False)
                assert not failed.stdout
                assert requests and all(path.startswith("/first/") for _, path, _ in requests), requests
                assert "private failure" not in failed.stderr
            first_status, first_body = None, None

            # All-local lists support search and the same first-package precedence.
            earlier = root / "earlier"
            earlier.mkdir()
            recipe(earlier, "shadow", "1.0.0")
            local_options = ("-registry", str(earlier), "-registry", str(local))
            assert run("search", *local_options, "shadow").stdout == "shadow\tshadow\n"
            dependency("shadow", ">=1.0.0")
            assert "shadow@1.0.0" in run("pretend", *local_options).stdout
            dependency("local-only")
            assert "local-only@1.0.0" in run("pretend", *local_options).stdout
            corrupt = recipe(earlier, "local-only")
            corrupt.write_text("invalid JSON")
            assert "Invalid JSON" in run("pretend", *local_options, ok=False).stderr
            assert "local registries" in run("search", "app", ok=False).stderr

            # Bad configuration does not become an implicit public fallback.
            for invalid in ([], "bad", [{"url": 42}], [{"url": first, "directory": str(local)}],
                            [{"url": first, "unexpected": True}], [{"directory": first}]):
                requests.clear()
                lock_file.write_text(json.dumps({"registries": invalid}))
                run("pretend", ok=False)
                assert not requests
            lock_file.write_text(json.dumps(dict(settings, registry=str(local))))
            assert "both registry and registries" in run("pretend", ok=False).stderr
            for value in ("file:///tmp/registry", "https://user:secret@example.invalid/registry",
                          "https://example.invalid/registry?secret=value"):
                failed = run("pretend", "-registry", value, ok=False)
                assert value not in failed.stderr
            run("pretend", "-registry", ok=False)
            run("-registry", ok=False)
        finally:
            server.shutdown()
            thread.join()

print("Ordered registry lookup, HTTP errors, source ownership, persistence and install/update passed")
