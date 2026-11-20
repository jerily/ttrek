"""Ordered local and HTTP registries, with isolated projects and HTTP fixtures."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
from urllib.parse import urlsplit

BINARY = Path(sys.argv[1]).resolve()

with tempfile.TemporaryDirectory(prefix="ttrek ordered registries ") as temporary:
    root = Path(temporary).resolve()
    home = root / "home"
    home.mkdir()
    project = root / "project"
    project.mkdir()
    local = root / "local registry"
    local.mkdir()
    env = dict(os.environ, HOME=str(home), TTREK_NO_UPDATE_CHECK="1", SHELL="/bin/sh",
               http_proxy="http://127.0.0.1:9", https_proxy="http://127.0.0.1:9",
               ALL_PROXY="http://127.0.0.1:9", NO_PROXY="127.0.0.1", no_proxy="127.0.0.1")

    def run(*args, ok=True, cwd=project, process_env=env):
        result = subprocess.run([BINARY, *args], cwd=cwd, env=process_env,
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
    batch_sources = set()
    batches = []
    response_limit = 64
    first_status = None
    first_body = None
    external_dependencies = {}

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_POST(self):
            requests.append((self.command, self.path, dict(self.headers)))
            source, _, kind = self.path.strip("/").split("/")
            if source not in batch_sources:
                self.send_error(500, "Unexpected POST")
                return
            payload = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
            batches.append((source, kind, payload))
            if len(payload.get("names", payload.get("items", []))) > response_limit:
                self.send_error(413)
                return
            if kind == "packages":
                body = {name: metadata[source].get(name, False) for name in payload["names"]}
                if payload["mode"] == "exists":
                    body = {name: bool(value) for name, value in body.items()}
            elif kind == "plan":
                body = {}
                for item in payload["items"]:
                    name, version = item["name"], item["version"]
                    versions = metadata[source][name]
                    assert version in versions
                    key = name + "@" + version
                    if item["field"] == "languages":
                        body[key] = ["C"]
                    elif name == "missing-recipe":
                        body[key] = False
                    else:
                        body[key] = {"version": version, "install_script": [], "languages": ["C"],
                                     "dependencies": versions[version], "scripts": {}}
            else:
                self.send_error(404)
                return
            data = json.dumps(body).encode()
            self.send_response(200)
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            requests.append((self.command, self.path, dict(self.headers)))
            parts = urlsplit(self.path).path.strip("/").split("/")
            source, name = (parts + ["", ""])[:2]
            if source == "registry":
                source = "second"  # Built-in public API, reached through a local test proxy.
            status = 200
            body = None
            if source in batch_sources and parts[1:] == ["batch", "capabilities"]:
                body = {"batch": 1}
            elif source in batch_sources and name == "search":
                body = ([{"name": n, "description": "", "match": "description"}
                         for n in ("public-only", "shared")] if source == "second" else [])
            elif source == "first" and first_status is not None:
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
                    if name in external_dependencies:
                        body["externalDependencies"] = external_dependencies[name]
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
            host_file = root / "host-sdk.h"
            external_dependencies["app"] = {"host-sdk": {
                "check": {"files": [str(host_file)]}, "installHint": "install host-sdk"}}
            failed = run("install", "-y", ok=False)
            assert "app@1.0.0 requires host-sdk" in failed.stderr and "install host-sdk" in failed.stderr
            assert "Package [" not in failed.stdout  # Even earlier dependencies must not build.
            host_file.touch()
            requests.clear()
            run("install", "-y")
            recipe_requests = [path for _, path, _ in requests if len(path.strip("/").split("/")) == 5]
            assert len(recipe_requests) == len(set(recipe_requests)) == 3, recipe_requests
            external_dependencies.clear()
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

            # All-local lists retain first-package precedence for resolution.
            earlier = root / "earlier"
            earlier.mkdir()
            recipe(earlier, "shadow", "1.0.0")
            local_options = ("-registry", str(earlier), "-registry", str(local))
            dependency("shadow", ">=1.0.0")
            assert "shadow@1.0.0" in run("pretend", *local_options).stdout
            dependency("local-only")
            assert "Nothing to install!" in run("pretend", *local_options).stdout
            corrupt = recipe(earlier, "local-only")
            corrupt.write_text("invalid JSON")
            assert "Invalid JSON" in run("pretend", *local_options, ok=False).stderr
            assert "search endpoint" in run("search", "app", ok=False).stderr

            # The shortcut always appends the public default, regardless of option placement.
            public = home / ".local/ttrek-registry"
            stamp = public / ".git/ttrek-last-sync"
            stamp.parent.mkdir(parents=True)
            stamp.write_text("synced")
            recipe(public, "public-only")
            recipe(public, "shadow", "9.0.0")
            dependency("public-only")
            lock_file.write_text(json.dumps({"registry": "../local registry"}))
            before = lock_file.read_bytes()
            run("pretend", ok=False)
            for args in (("pretend", "-add-pubreg"),
                         ("-add-pubreg", "pretend"),
                         ("-add-pubreg", "-registry", str(local), "pretend"),
                         ("-registry", str(local), "pretend", "-add-pubreg"),
                         ("pretend", "-add-pubreg", "-registry", str(local), "-add-pubreg")):
                assert "Nothing to install!" in run(*args).stdout
            assert lock_file.read_bytes() == before
            assert run("gen-script", "-add-pubreg").stdout.startswith("#!")
            run("install", "-y", "-add-pubreg")
            run("update", "-y", "-add-pubreg")
            assert json.loads(lock_file.read_text())["registry"] == "../local registry"
            assert "registries" not in json.loads(lock_file.read_text())
            dependency("shadow", ">=1.0.0")
            assert "shadow@3.0.0" in run("-add-pubreg", "pretend", "-registry", str(local)).stdout

            # init persists a portable public marker, including when it is the only source.
            portable = root / "portable"
            portable.mkdir()
            run("-add-pubreg", "init", "-y", "-registry", "../local registry", "-add-pubreg", cwd=portable)
            saved = {"registries": [{"directory": "../local registry"}, {"public": True}]}
            assert json.loads((portable / "ttrek-lock.json").read_text())["registries"] == saved["registries"]
            (portable / "ttrek.json").write_text(json.dumps({"dependencies": {"public-only": "1.0.0"}}))
            assert "public-only@1.0.0" in run("pretend", cwd=portable).stdout
            public_project = root / "public project"
            public_project.mkdir()
            run("init", "-y", "-add-pubreg", cwd=public_project)
            assert json.loads((public_project / "ttrek-lock.json").read_text())["registries"] == [{"public": True}]
            dependency("public-only")
            lock_file.write_text(json.dumps(saved))
            assert "Nothing to install!" in run("pretend").stdout
            # Explicit sources replace the saved public fallback unless the flag is supplied again.
            run("pretend", "-registry", str(local), ok=False)
            assert "Nothing to install!" in run("pretend", "-registry", str(local), "-add-pubreg").stdout

            # Without a completed sync, the marker resolves to the public API, not the directory.
            stamp.unlink()
            public_env = dict(env, ENV="LOCAL", http_proxy=base, NO_PROXY="", no_proxy="", ALL_PROXY="")
            requests.clear()
            assert "Nothing to install!" in run("pretend", process_env=public_env).stdout
            assert run("gen-script", "-registry", str(local), "-add-pubreg", process_env=public_env).stdout.startswith("#!")
            assert requests and all(path.startswith("http://localhost:8080/registry/") for _, path, _ in requests)
            assert all(method == "GET" and "Authorization" not in headers and
                       "TTrek-Environment-Id" not in headers for method, _, headers in requests)
            assert "search endpoint" in run("search", "public-only", process_env=public_env, ok=False).stderr
            stamp.write_text("synced")

            # Exercise batched and legacy private registries with the same dependency graph.
            dependency("app")
            for enabled in ({"first", "second"}, {"first"}, {"second"}):
                batch_sources.clear()
                batch_sources.update(enabled)
                batches.clear()
                requests.clear()
                assert run("gen-script", *options).stdout.startswith("#!")
                for source in enabled:
                    plans = [p["items"] for registry, kind, p in batches if registry == source and kind == "plan"]
                    assert len(plans) == 1, batches
                    expected = {"app", "shared"} if source == "first" else {"public-only"}
                    assert {item["name"] for item in plans[0]} == expected
                    assert all(item["field"] == "recipe" for item in plans[0])
                for source, kind, payload in batches:
                    if kind == "packages" and source == "second":
                        assert not {"app", "shared"}.intersection(payload["names"])
                if "first" in enabled:
                    assert any({"shared", "public-only"}.issubset(p["names"]) for registry, kind, p in batches
                               if registry == "first" and kind == "packages"), batches
                assert all("TTrek-Environment-Id" not in headers for _, _, headers in requests)
            batch_sources.update({"first", "second"})
            batches.clear()
            run("pretend", *options)
            assert all(item["field"] == "languages" for _, kind, p in batches if kind == "plan" for item in p["items"])
            batches.clear()
            assert run("search", "-registry", first, "-registry", second, "tcl").stdout.startswith("public-only")
            assert len(batches) == 1 and batches[0][:2] == ("first", "packages")
            assert batches[0][2]["mode"] == "exists" and set(batches[0][2]["names"]) == {"shared", "public-only"}
            dependency("missing-recipe")
            batches.clear()
            assert "missing-recipe@1.0.0" in run("gen-script", *options, ok=False).stderr
            assert all(source == "first" for source, _, _ in batches)
            # Full batches respect the item limit and split a rejected response in place.
            large = ["bulk-" + str(i) for i in range(65)]
            metadata["first"].update({n: {"1.0.0": {}} for n in large})
            spec["dependencies"] = {n: "1.0.0" for n in large}
            spec_file.write_text(json.dumps(spec))
            batches.clear()
            run("pretend", *options)
            assert [len(p["items"]) for _, kind, p in batches if kind == "plan"] == [64, 1]
            response_limit = 32
            batches.clear()
            run("gen-script", *options)
            assert [len(p["items"]) for _, kind, p in batches if kind == "plan"] == [64, 32, 32, 1]
            batch_sources.clear()

            # Bad configuration does not become an implicit public fallback.
            for invalid in ([], "bad", [{"url": 42}], [{"url": first, "directory": str(local)}],
                            [{"url": first, "unexpected": True}], [{"directory": first}],
                            [{"public": False}], [{"public": "true"}], [{"public": True, "url": first}],
                            [{"public": True}, {"directory": str(local)}], [{"public": True}, {"public": True}]):
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
