"""Linux static-libcurl trust-store selection and TLS verification."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import argparse
import json
import os
from pathlib import Path
import ssl
import shutil
import subprocess
import tempfile
import threading

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("binary", type=Path)
parser.add_argument("openssl")
parser.add_argument("fixture", type=Path, nargs="?")
parser.add_argument("--system-trust", action="store_true",
                    help="temporarily install the test CA; use only in disposable runtime-test containers")
args = parser.parse_args()
BINARY = args.binary.resolve()
OPENSSL = args.openssl
FIXTURE = args.fixture.resolve() if args.fixture else None

with tempfile.TemporaryDirectory(prefix="ttrek https ") as temporary:
    root = Path(temporary)
    cert, key = root / "cert.pem", root / "key.pem"
    config = root / "openssl.cnf"
    config.write_text("[req]\ndistinguished_name=dn\n[dn]\n")
    subprocess.run([OPENSSL, "req", "-x509", "-newkey", "rsa:2048", "-nodes",
                    "-config", str(config),
                    "-days", "1", "-keyout", str(key), "-out", str(cert),
                    "-subj", "/CN=localhost", "-addext", "basicConstraints=critical,CA:TRUE",
                    "-addext", "subjectAltName=DNS:localhost"],
                   check=True, capture_output=True)
    (root / "ttrek.json").write_text(json.dumps({"dependencies": {"probe": "1.0.0"}}))
    home = root / "home"
    home.mkdir()
    env = {key: value for key, value in os.environ.items()
           if key.lower() not in ("http_proxy", "https_proxy", "all_proxy")
           and key not in ("CURL_CA_BUNDLE", "SSL_CERT_FILE", "SSL_CERT_DIR", "ENV")}
    env.update(HOME=str(home), TTREK_NO_UPDATE_CHECK="1", NO_PROXY="localhost,127.0.0.1",
               no_proxy="localhost,127.0.0.1", http_proxy="http://127.0.0.1:9",
               https_proxy="http://127.0.0.1:9", ALL_PROXY="http://127.0.0.1:9")

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def handle(self):
            try:
                super().handle()
            except (ConnectionResetError, ssl.SSLError):
                # Certificate/hostname rejection can close the socket before HTTP.
                pass

        def do_GET(self):
            if self.path.startswith("/releases/latest/"):
                data = json.dumps({"version": "2.0.0", "target": self.path.rsplit("/", 1)[1],
                                   "url": "https://localhost/binary", "sha256": "0" * 64}).encode()
            elif self.path == "/registry/probe":
                data = b'{"1.0.0": {}}'
            else:
                data = b"verified HTTPS download\n"
            self.send_response(200)
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

    def run(*args, variables=None, ok=True, binary=BINARY):
        result = subprocess.run([str(binary), *map(str, args)], cwd=root,
                                env=dict(env, **(variables or {})),
                                text=True, capture_output=True, timeout=30)
        assert (result.returncode == 0) == ok, (args, result.stdout, result.stderr)
        return result

    with ThreadingHTTPServer(("127.0.0.1", 0), Handler) as server:
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(cert, key)
        server.socket = context.wrap_socket(server.socket, server_side=True)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            url = "https://localhost:{}".format(server.server_port)
            output = root / "download"
            # Resolve localhost; trusting its issuer must not bypass hostname checks.
            for variable in ("CURL_CA_BUNDLE", "SSL_CERT_FILE"):
                trust = {variable: str(cert)}
                run("download", url + "/file", output, variables=trust)
                assert output.read_bytes() == b"verified HTTPS download\n"
                assert "probe@1.0.0" in run("pretend", "-registry", url + "/registry", variables=trust).stdout
                if FIXTURE:
                    release_env = dict(trust, TTREK_TEST_RELEASE_ROOT=url)
                    assert "2.0.0 is available" in run("self-update", "-check", variables=release_env, binary=FIXTURE).stdout
                run("download", url.replace("localhost", "127.0.0.1") + "/file", output,
                    variables=trust, ok=False)
                assert not output.exists()
            run("download", url + "/file", output, ok=False)
            assert not output.exists()
            run("pretend", "-registry", url + "/registry", ok=False)
            run("download", url + "/file", output,
                variables={"CURL_CA_BUNDLE": str(root / "missing.pem"), "SSL_CERT_FILE": str(cert)}, ok=False)
            assert not output.exists()
            if args.system_trust:
                if shutil.which("update-ca-trust"):
                    anchor = Path("/etc/pki/ca-trust/source/anchors/ttrek-runtime-test.crt")
                    update = ["update-ca-trust", "extract"]
                else:
                    anchor = Path("/usr/local/share/ca-certificates/ttrek-runtime-test.crt")
                    update = ["update-ca-certificates"]
                # Exclusive creation avoids replacing any existing administrator file.
                with anchor.open("xb") as output_cert:
                    output_cert.write(cert.read_bytes())
                try:
                    subprocess.run(update, check=True, capture_output=True)
                    run("download", url + "/file", output)
                    assert output.read_bytes() == b"verified HTTPS download\n"
                    assert "probe@1.0.0" in run("pretend", "-registry", url + "/registry").stdout
                finally:
                    anchor.unlink()
                    subprocess.run(update, check=True, capture_output=True)
        finally:
            server.shutdown()
            thread.join()

print("HTTPS downloads, registries, trust overrides, untrusted certificates and hostname checks passed")
