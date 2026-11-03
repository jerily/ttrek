"""Exercise registry synchronization, local precedence and atomic self-update offline."""
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import platform
import pty
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from urllib.parse import urlsplit

BINARY = Path(sys.argv[1]).resolve()
PRODUCTION = Path(sys.argv[2]).resolve()

with tempfile.TemporaryDirectory(prefix="ttrek maintenance ") as temporary:
    root = Path(temporary)
    home = root / "home"
    home.mkdir()
    project = root / "project"
    project.mkdir()
    (project / "ttrek.json").write_text(json.dumps({"name": "demo", "version": "1.0.0", "dependencies": {"test-package": "1.0.0"}}))
    mirror = root / "remote"
    env = dict(os.environ, HOME=str(home), GIT_CONFIG_GLOBAL=str(root / "gitconfig"),
               GIT_CONFIG_NOSYSTEM="1", GIT_TERMINAL_PROMPT="0", TTREK_NO_UPDATE_CHECK="1")

    def run(*args, ok=True, cwd=project, binary=BINARY, process_env=env, **kwargs):
        result = subprocess.run([str(binary), *args], cwd=cwd, env=process_env,
                                capture_output=True, text=True, timeout=15, **kwargs)
        assert (result.returncode == 0) == ok, (args, result.stdout, result.stderr)
        return result

    def git(*args):
        return subprocess.run(["git", *args], env=env, capture_output=True, text=True, check=True).stdout.strip()

    git("init", "-b", "main", str(mirror))
    git("-C", str(mirror), "config", "user.name", "Test")
    git("-C", str(mirror), "config", "user.email", "test@example.invalid")
    recipe = mirror / "test-package/1.0.0/ttrek.json"
    recipe.parent.mkdir(parents=True)
    recipe.write_text(json.dumps({"name": "test-package", "version": "1.0.0", "dependencies": {}, "build": {"default": []}}))
    git("-C", str(mirror), "add", ".")
    git("-C", str(mirror), "commit", "-m", "initial")
    git("config", "--global", f"url.{mirror.as_uri()}.insteadOf", "https://github.com/jerily/ttrek-registry.git")

    checkout = home / ".local/ttrek-registry"
    registry_fixture = root / "empty-registry.json"
    registry_fixture.write_text("{}")
    env["TTREK_TEST_REGISTRY"] = str(registry_fixture)
    run("gen-script", ok=False)
    assert not checkout.exists()
    run("sync")
    assert (checkout / "test-package/1.0.0/ttrek.json").exists()
    assert (checkout / ".git/ttrek-last-sync").exists()
    initialized = root / "initialized"
    initialized.mkdir()
    run("init", "-y", cwd=initialized)
    assert "registry" not in json.loads((initialized / "ttrek-lock.json").read_text())
    # Auto-selection uses the real local-registry reader, with the fixture API empty.
    stamp = checkout / ".git/ttrek-last-sync"
    stamp.unlink()
    run("gen-script", ok=False)  # An unmarked directory does not activate local mode.
    run("sync")
    generated = run("gen-script")
    assert generated.stdout.startswith("#!")
    assert "test-package" in generated.stdout
    assert "run 'ttrek sync'" not in generated.stderr
    old = time.time() - 90000
    os.utime(checkout / ".git/ttrek-last-sync", (old, old))
    stale = run("gen-script")
    assert stale.stdout == generated.stdout
    assert "run 'ttrek sync'" in stale.stderr
    (project / "ttrek-lock.json").write_text(json.dumps({"registry": str(checkout)}))
    assert "run 'ttrek sync'" in run("gen-script").stderr
    (project / "ttrek-lock.json").unlink()

    explicit = root / "explicit"
    explicit.mkdir()
    run("gen-script", "-registry", str(explicit), ok=False)
    (project / "ttrek-lock.json").write_text(json.dumps({"registry": str(explicit)}))
    run("gen-script", ok=False)
    assert run("gen-script", "-registry", str(checkout)).stdout == generated.stdout
    (project / "ttrek-lock.json").unlink()

    (mirror / "next").write_text("next")
    git("-C", str(mirror), "add", ".")
    git("-C", str(mirror), "commit", "-m", "next")
    run("sync")
    assert (checkout / "next").read_text() == "next"
    (checkout / "dirty").write_text("keep")
    run("sync", ok=False)
    assert (checkout / "dirty").read_text() == "keep"
    (checkout / "dirty").unlink()
    git("-C", str(checkout), "switch", "-c", "topic")
    run("sync", ok=False)
    assert git("-C", str(checkout), "branch", "--show-current") == "topic"
    git("-C", str(checkout), "switch", "main")
    # Explicit current-directory and paths with spaces are supported.
    current = root / "current"
    current.mkdir()
    run("sync", ".", cwd=current)
    assert (current / ".git").exists()
    run("sync", str(root / "separate checkout"))
    divergent = root / "separate checkout"
    git("-C", str(divergent), "config", "user.name", "Test")
    git("-C", str(divergent), "config", "user.email", "test@example.invalid")
    (divergent / "local-change").write_text("preserve")
    git("-C", str(divergent), "add", ".")
    git("-C", str(divergent), "commit", "-m", "local")
    divergent_head = git("-C", str(divergent), "rev-parse", "HEAD")
    (mirror / "remote-change").write_text("remote")
    git("-C", str(mirror), "add", ".")
    git("-C", str(mirror), "commit", "-m", "remote")
    run("sync", str(divergent), ok=False)
    assert git("-C", str(divergent), "rev-parse", "HEAD") == divergent_head
    assert (divergent / "local-change").read_text() == "preserve"
    run("sync", str(project), ok=False)
    run("sync", "--bogus", ok=False)
    original_head = git("-C", str(checkout), "rev-parse", "HEAD")
    git("-C", str(checkout), "remote", "set-url", "origin", "https://example.invalid/wrong.git")
    run("sync", ok=False)
    assert git("-C", str(checkout), "rev-parse", "HEAD") == original_head
    git("-C", str(checkout), "remote", "set-url", "origin", "https://github.com/jerily/ttrek-registry.git")

    machine = "arm64" if platform.machine() in ("arm64", "aarch64") else "amd64"
    target = ("macos-" + ("arm64" if machine == "arm64" else "x86_64")) if sys.platform == "darwin" else f"linux-{machine}-" + ("glibc" if platform.libc_ver()[0] == "glibc" else "musl")
    # A tiny executable implements the release version probe without another compiler target.
    payload = b'#!/bin/sh\nprintf "2.0.0\\n"\n'
    metadata = {"version": "2.0.0", "target": target, "sha256": hashlib.sha256(payload).hexdigest()}
    requests = []

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_GET(self):
            requests.append(self.path)
            path = urlsplit(self.path).path
            if path == f"/releases/latest/{target}":
                body = json.dumps(metadata).encode()
            elif path == "/binary":
                body = payload
            else:
                self.send_error(404)
                return
            self.send_response(200)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    with ThreadingHTTPServer(("127.0.0.1", 0), Handler) as server:
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            address = f"http://127.0.0.1:{server.server_port}"
            env["TTREK_TEST_RELEASE_ROOT"] = address
            metadata["url"] = address + "/binary"
            # Use a local HTTP proxy to verify the production LOCAL endpoint without
            # binding port 8080 or contacting an existing development server.
            local_env = dict(env, ENV="LOCAL", http_proxy=address, NO_PROXY="", no_proxy="", ALL_PROXY="")
            local_env.pop("TTREK_TEST_RELEASE_ROOT")
            assert "2.0.0 is available" in run("self-update", "-check", binary=PRODUCTION, process_env=local_env).stdout
            assert requests[-1] == f"http://localhost:8080/releases/latest/{target}"
            stamp = checkout / ".git/ttrek-last-sync"
            stamp_content = stamp.read_bytes()
            stamp.unlink()
            run("pretend", binary=PRODUCTION, process_env=local_env, ok=False)
            assert requests[-1].startswith("http://localhost:8080/registry/")
            stamp.write_bytes(stamp_content)
            assert run("self-update", "-version").stdout.strip() == "1.0.0"
            assert "2.0.0 is available" in run("self-update", "-check").stdout
            assert "/binary" not in requests
            candidate = root / "installed ttrek"
            shutil.copy2(BINARY, candidate)
            original = candidate.read_bytes()
            metadata["sha256"] = "0" * 64
            run("self-update", binary=candidate, ok=False)
            assert candidate.read_bytes() == original
            assert not list(root.glob(".ttrek-update-*"))
            metadata["sha256"] = hashlib.sha256(payload).hexdigest()
            metadata["target"] = "wrong-platform"
            run("self-update", binary=candidate, ok=False)
            assert candidate.read_bytes() == original
            metadata["target"] = target
            metadata["version"] = "1.0.0"
            assert "up to date" in run("self-update", binary=candidate).stdout
            assert candidate.read_bytes() == original
            metadata["version"] = "3.0.0"
            run("self-update", binary=candidate, ok=False)
            assert candidate.read_bytes() == original
            metadata["version"] = "2.0.0"
            link = root / "ttrek-link"
            link.symlink_to(candidate)
            run("self-update", binary=link)
            assert link.is_symlink() and candidate.read_bytes() == payload
            assert candidate.stat().st_mode & 0o111
            assert not list(root.glob(".ttrek-update-*"))

            # Interactive checks notify once per day and do not modify command success.
            interactive_env = dict(env)
            interactive_env.pop("TTREK_NO_UPDATE_CHECK")
            master, slave = pty.openpty()
            try:
                result = subprocess.run([str(BINARY), "sync"], env=interactive_env, cwd=project,
                                        stdout=subprocess.PIPE, stderr=slave, timeout=15)
                assert result.returncode == 0
                os.close(slave)
                slave = None
                output = os.read(master, 65536).decode()
                assert "ttrek 2.0.0 is available" in output
            finally:
                if slave is not None:
                    os.close(slave)
                os.close(master)
            before = len(requests)
            master, slave = pty.openpty()
            try:
                result = subprocess.run([str(BINARY), "sync"], env=interactive_env, cwd=project,
                                        stdout=subprocess.PIPE, stderr=slave, timeout=15)
                assert result.returncode == 0
            finally:
                os.close(slave)
                os.close(master)
            assert len(requests) == before
        finally:
            server.shutdown()
            thread.join()

    # Offline explicit upgrade preserves the binary; automatic checks never fail sync.
    offline_env = dict(env, TTREK_TEST_RELEASE_ROOT="http://127.0.0.1:1")
    shutil.copy2(BINARY, candidate)
    run("self-update", binary=candidate, process_env=offline_env, ok=False)
    assert candidate.read_bytes() == original
    (home / ".local/state/ttrek/update-check").unlink()
    offline_env.pop("TTREK_NO_UPDATE_CHECK")
    master, slave = pty.openpty()
    try:
        result = subprocess.run([str(BINARY), "sync"], env=offline_env, cwd=project,
                                stdout=subprocess.PIPE, stderr=slave, timeout=10)
        assert result.returncode == 0
    finally:
        os.close(slave)
        os.close(master)
print("Registry sync, precedence, freshness, upgrade integrity/atomicity and offline checks passed")
