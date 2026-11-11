"""Telemetry opt-out must suppress collection, IDs, headers and events."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
from urllib.parse import urlsplit

binary, cmake, curl = (Path(arg).absolute() for arg in sys.argv[1:])

with tempfile.TemporaryDirectory(prefix="ttrek-telemetry-") as directory:
    root = Path(directory)
    home, project, tools = (root / name for name in ("home", "project", "bin"))
    for path in (home, project, tools):
        path.mkdir()
    (tools / "cmake").symlink_to(cmake)
    (tools / "curl").symlink_to(curl)
    compiler_log = root / "compiler.log"
    compiler = tools / "compiler"
    compiler.write_text('#!/bin/sh\nprintf "collected\\n" >> "$TTREK_TEST_COMPILER_LOG"\nprintf "fixture compiler 1.0\\n"\n')
    compiler.chmod(0o755)
    requests = []
    available = "1.0.0"

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def respond(self, body):
            data = json.dumps(body).encode()
            self.send_response(200)
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            path = urlsplit(self.path).path
            requests.append(("GET", path, dict(self.headers)))
            if path == "/registry/demo":
                self.respond({available: {}})
            elif path.startswith("/registry/demo/" + available + "/"):
                self.respond({"version": available, "dependencies": {}, "install_script": []})
            else:
                self.send_error(404)

        def do_POST(self):
            path = urlsplit(self.path).path
            self.rfile.read(int(self.headers.get("Content-Length", "0")))
            requests.append(("POST", path, dict(self.headers)))
            self.respond({})

    with ThreadingHTTPServer(("127.0.0.1", 0), Handler) as server:
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        address = f"http://127.0.0.1:{server.server_port}"
        # Proxy the implicit LOCAL endpoint; an explicit -registry disables
        # telemetry independently and would not exercise the environment switch.
        env = dict(os.environ, HOME=str(home), PATH=str(tools) + os.pathsep + os.environ["PATH"],
                   SHELL="/bin/sh", TTREK_NO_UPDATE_CHECK="1", TTREK_FAIL_VERBOSE="1",
                   TTREK_TELEMETRY="0", ENV="LOCAL", http_proxy=address, https_proxy=address,
                   ALL_PROXY=address, NO_PROXY="", no_proxy="", CC=str(compiler), CXX=str(compiler),
                   TTREK_TEST_COMPILER_LOG=str(compiler_log))

        def run(*args, process_env=env):
            result = subprocess.run([binary, *args], cwd=project, env=process_env,
                                    capture_output=True, text=True, timeout=30)
            assert result.returncode == 0, (args, result.stdout, result.stderr)
            return result

        def opted_out():
            assert requests and all(method == "GET" and path.startswith("/registry/") and
                                   "TTrek-Environment-Id" not in headers
                                   for method, path, headers in requests), requests
            assert not compiler_log.exists()

        try:
            run("init", "-y")
            machine_id = home / ".ttrek/machine-id"
            assert not machine_id.exists() and not requests
            run("install", "-y", "demo")
            opted_out()
            assert not machine_id.exists()

            # Previously saved identities must not bypass the opt-out.
            machine_id.parent.mkdir(parents=True, exist_ok=True)
            machine_id.write_text("test-machine")
            available = "1.1.0"
            requests.clear()
            run("update", "-y")
            opted_out()
            assert json.loads((project / "ttrek-lock.json").read_text())["packages"]["demo"]["version"] == available

            # Both the explicit opt-in and the unset default retain telemetry.
            for setting in ("1", None):
                enabled = dict(env)
                if setting is None:
                    enabled.pop("TTREK_TELEMETRY")
                else:
                    enabled["TTREK_TELEMETRY"] = setting
                requests.clear()
                assert "Telemetry: on\n" in run("version", process_env=enabled).stdout
                assert not requests and not compiler_log.exists()
                run("install", "-y", "-force", "demo", process_env=enabled)
                paths = [path for method, path, _ in requests if method == "POST"]
                assert "/telemetry/register/test-machine" in paths, requests
                assert f"/telemetry/collect/demo/{available}" in paths, requests
                assert all(headers.get("TTrek-Environment-Id") == "test-machine" for _, _, headers in requests)
                assert compiler_log.read_text().splitlines() == ["collected", "collected"]
                compiler_log.unlink()

            requests.clear()
            run("install", "-y", "-force", "demo")
            opted_out()
            assert machine_id.read_text() == "test-machine"

            # Enabling the switch never overrides the explicit-registry policy.
            requests.clear()
            run("install", "-y", "-force", "demo", "-registry", address + "/registry",
                process_env=dict(env, TTREK_TELEMETRY="1"))
            opted_out()
        finally:
            server.shutdown()
            thread.join()

print("Telemetry opt-out, saved IDs, request headers, events and default behavior passed")
