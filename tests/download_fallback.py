"""Recipe-labelled mirror keys, diagnostic filenames, and signed redirects."""
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
from urllib.parse import parse_qs, urlsplit

binary = Path(sys.argv[1]).resolve()
requests = []
payload = b"mirrored source archive\n"
class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass
    def do_GET(self):
        requests.append(self.path)
        path = urlsplit(self.path).path
        if path.startswith('/download/'):
            self.send_response(302)
            self.send_header('Location', '/signed-object?X-Amz-Signature=' + 'a' * 64)
            self.end_headers()
        elif path == '/signed-object':
            self.send_response(200)
            self.end_headers()
            self.wfile.write(payload)
        else:
            self.send_error(404)

with tempfile.TemporaryDirectory(prefix="ttrek-fallback-") as temporary, ThreadingHTTPServer(('127.0.0.1', 0), Handler) as server:
    root = Path(temporary)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = f'http://127.0.0.1:{server.server_port}'
    url = base + '/unavailable?version=1'
    digest = hashlib.sha256(url.encode()).hexdigest()
    filename = "nested/archive ' & $.tar.gz"
    env = dict(os.environ, TTREK_NO_UPDATE_CHECK='1', TTREK_TEST_DOWNLOAD_ROOT=base + '/download')
    def run(*args, ok=True):
        result = subprocess.run([binary, *args], cwd=root, env=env, text=True, capture_output=True, timeout=30)
        assert (result.returncode == 0) == ok, (args, result.stdout, result.stderr)
        return result
    output = root / 'archive'
    run('download', url, output, 'example-1.0.0', filename)
    assert output.read_bytes() == payload
    mirrored = [urlsplit(path) for path in requests if path.startswith('/download/')]
    assert len(mirrored) == 1 and mirrored[0].path == '/download/example-1.0.0-' + digest + '.archive'
    assert parse_qs(mirrored[0].query) == {'filename': [filename]}
    requests.clear()
    run('download', url, output, 'example-1.0.0')
    assert any(path == '/download/example-1.0.0-' + digest + '.archive' for path in requests)
    requests.clear()
    run('download', url, output, '../bad', ok=False)
    assert not requests
    registry = root / 'registry/example/1.0.0'
    registry.mkdir(parents=True)
    (registry / 'ttrek.json').write_text(json.dumps({'name': 'example', 'version': '1.0.0', 'dependencies': {}, 'build': {'default': [
        {'cmd': 'download', 'url': url, 'filename': filename}]}}))
    run('init', '-y', '-registry', str(root / 'registry'))
    for shell in ('sh', 'bash'):
        requests.clear()
        directory = root / shell
        directory.mkdir()
        script = directory / 'build.sh'
        script.write_text(run('gen-script', '-shell', shell, 'example@1.0.0').stdout)
        result = subprocess.run([shell, script], env=env, text=True, capture_output=True, timeout=30)
        assert result.returncode == 0, (result.stdout, result.stderr)
        assert (directory / 'build/download' / filename).read_bytes() == payload
        mirrored = [urlsplit(path) for path in requests if path.startswith('/download/')]
        assert len(mirrored) == 1 and parse_qs(mirrored[0].query) == {'filename': [filename]}
    server.shutdown()
print('fallback: recipe/URL SHA-256 keys, filename encoding, redirects, native/Bash/sh passed')
