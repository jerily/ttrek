"""Release orchestration checks; no Docker builds or AWS requests."""
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import threading
import unittest

ROOT = Path(__file__).resolve().parents[1]


class Releases(unittest.TestCase):
    def setUp(self):
        # Snap-packaged curl cannot read the host's /tmp. Keep its inputs under
        # the repository's ignored build directory, as real builds do.
        (ROOT / "build").mkdir(exist_ok=True)
        temporary = tempfile.TemporaryDirectory(prefix="ttrek releases ", dir=ROOT / "build")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        shutil.copytree(ROOT / "bin", self.root / "bin")
        self.commands = self.root / "commands"
        self.commands.mkdir()
        self.log = self.root / "commands.jsonl"
        for name in ("aws", "docker"):
            command = self.commands / name
            command.write_text("#!/usr/bin/env python3\n"
                "import json, os, sys\n"
                "with open(os.environ['COMMAND_LOG'], 'a') as log:\n"
                "    log.write(json.dumps(sys.argv[1:]) + '\\n')\n"
                "sys.exit(int(os.environ.get('COMMAND_FAILURE', '0')))\n")
            command.chmod(0o755)
        self.manifest = self.root / "releases.json"
        self.env = {**os.environ, "PATH": f"{self.commands}:{os.environ['PATH']}",
            "PROJECT_HOME": str(self.root), "COMMAND_LOG": str(self.log),
            "TTREK_CHECKSUMS_FILE": str(self.manifest),
            "TTREK_LOCAL_S3_DIR": str(self.root / "seed"), "AWS_PROFILE": "test-profile",
            "AWS_REGION": "eu-central-1"}
        for key in ("TTREK_S3_BUCKET", "TTREK_S3_PREFIX", "TTREK_LOCAL_S3_ENDPOINT"):
            self.env.pop(key, None)

    def binary(self, target, data=b"release\x00\xff\n"):
        binary = self.root / "dist" / target / "ttrek"
        binary.parent.mkdir(parents=True, exist_ok=True)
        binary.write_bytes(data)
        return hashlib.sha256(data).hexdigest()

    def run_script(self, name, *args, success=True):
        result = subprocess.run([str(self.root / "bin" / name), *args],
            env=self.env, cwd="/tmp", capture_output=True, text=True)
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
        return result

    def test_linux_aliases_select_platform_image_and_output(self):
        scripts = json.loads((ROOT / "ttrek.json").read_text())["scripts"]
        self.env["JOBS"] = "3"
        for architecture in ("amd64", "arm64"):
            for libc in ("glibc", "musl"):
                target = f"linux-{architecture}-{libc}"
                subprocess.run(["bash", "-c", scripts[f"prepare-{target}"]],
                    env=self.env, cwd="/tmp", check=True)
                arguments = json.loads(self.log.read_text().splitlines()[-1])
                self.assertEqual(arguments[arguments.index("--platform") + 1], f"linux/{architecture}")
                image = next(arg.split("=", 1)[1] for arg in arguments if arg.startswith("BASE_IMAGE="))
                if libc == "glibc":
                    suffix = "x86_64" if architecture == "amd64" else "aarch64"
                    self.assertRegex(image, rf"^quay.io/pypa/manylinux_2_28_{suffix}@sha256:[a-f0-9]{{64}}$")
                else:
                    self.assertEqual(image, "alpine:3.23")
                self.assertIn(f"LIBC={libc}", arguments)
                self.assertIn("JOBS=3", arguments)
                self.assertIn(f"type=local,dest=dist/{target}", arguments)

    def test_macos_11_builds_supported_architectures_and_combines_once(self):
        for name, body in {
            "uname": 'echo Darwin',
            "sysctl": 'echo "$TEST_ARM64"',
        }.items():
            command = self.commands / name
            command.write_text("#!/bin/sh\n" + body + "\n")
            command.chmod(0o755)
        for name in ("prepare-macos.sh", "prepare-universal2.sh", "prepare-linux.sh", "test-linux-release.sh"):
            script = self.root / "bin" / name
            script.write_text('#!/bin/sh\nprintf "%s %s %s\\n" "' + name +
                '" "$*" "$MACOSX_DEPLOYMENT_TARGET" >> "$COMMAND_LOG"\n')
        self.env["MACOSX_DEPLOYMENT_TARGET"] = "15.0"
        self.env["TEST_ARM64"] = "1"
        self.run_script("prepare-macos-target-11.0.sh")
        self.assertEqual(self.log.read_text().splitlines(), [
            "prepare-macos.sh arm64 11.0", "prepare-macos.sh x86_64 11.0",
            "prepare-universal2.sh  11.0", "prepare-linux.sh arm64 glibc 11.0",
            "prepare-linux.sh arm64 musl 11.0", "test-linux-release.sh arm64 11.0"])
        self.log.unlink()
        self.env["TEST_ARM64"] = "0"
        self.run_script("prepare-macos-target-11.0.sh")
        self.assertEqual(self.log.read_text(), "prepare-macos.sh x86_64 11.0\n")

    def test_invalid_inputs_never_upload(self):
        for arguments in ((), ("staging", "1.0.0"), ("prod", "latest"),
                          ("prod", "1.0.0"), ("prod", "1.0.0", "../secret")):
            self.run_script("upload.sh", *arguments, success=False)
        self.binary("linux-amd64-glibc")
        self.run_script("upload.sh", "prod", "1.0.0", "linux-amd64-glibc", "macos-arm64", success=False)
        self.assertFalse(self.log.exists())

    def test_runtime_matrix_uses_pinned_images_and_reruns_verification(self):
        for architecture in ("amd64", "arm64"):
            for libc in ("glibc", "musl"):
                self.binary(f"linux-{architecture}-{libc}")
            self.run_script("test-linux-release.sh", architecture)
            commands = [json.loads(line) for line in self.log.read_text().splitlines()[-3:]]
            for arguments, image in zip(commands, ("almalinux:8.10", "public.ecr.aws/amazonlinux/amazonlinux:2023", "ubuntu:24.04")):
                self.assertIn("BASE_IMAGE=" + image + "@sha256:", " ".join(arguments))
                self.assertEqual(arguments[arguments.index("--platform") + 1], f"linux/{architecture}")
                self.assertEqual(arguments[arguments.index("--no-cache-filter") + 1], "verify")
                self.assertIn("type=cacheonly", arguments)

    def test_runtime_matrix_requires_both_binaries_and_valid_arguments(self):
        for arguments in ((), ("riscv64",), ("amd64", "unknown"), ("amd64", "ubuntu", "extra"), ("amd64",)):
            self.run_script("test-linux-release.sh", *arguments, success=False)
        self.binary("linux-amd64-glibc")
        self.run_script("test-linux-release.sh", "amd64", success=False)
        self.assertFalse(self.log.exists())

    def test_history_targets_and_environments_survive_merging(self):
        expected = {"local": {"1.0.0": {"macos-arm64": "b" * 64}},
                    "prod": {"0.9.0": {"macos-arm64": "c" * 64},
                             "1.0.0": {"macos-arm64": "a" * 64}}}
        self.manifest.write_text(json.dumps(expected))
        checksum = self.binary("linux-amd64-glibc")
        self.run_script("upload.sh", "prod", "1.0.0")
        expected["prod"]["1.0.0"]["linux-amd64-glibc"] = checksum
        self.assertEqual(json.loads(self.manifest.read_text()), expected)
        self.assertEqual(self.manifest.stat().st_mode & 0o777, 0o644)
        arguments = json.loads(self.log.read_text())
        self.assertIn("s3://jerily-ttrek-app/ttrek/1.0.0/linux-amd64-glibc/ttrek", arguments)
        self.assertIn("test-profile", arguments)
        self.binary("linux-amd64-glibc", b"next version")
        self.run_script("upload.sh", "prod", "1.1.0")
        self.assertEqual(json.loads(self.manifest.read_text())["prod"]["1.0.0"], expected["prod"]["1.0.0"])

    def test_failure_leaves_manifest_unchanged_and_cleans_temporary_file(self):
        self.manifest.write_text('{"prod": {}}\n')
        self.binary("macos-arm64")
        self.env["COMMAND_FAILURE"] = "1"
        self.run_script("upload.sh", "prod", "1.0.0", success=False)
        self.assertEqual(self.manifest.read_text(), '{"prod": {}}\n')
        self.assertEqual(list(self.root.glob("releases.json.*")), [])

    def test_production_cannot_replace_recorded_bytes(self):
        self.binary("macos-arm64")
        self.run_script("upload.sh", "prod", "1.0.0")
        original = self.manifest.read_bytes()
        self.binary("macos-arm64", b"different")
        self.run_script("upload.sh", "prod", "1.0.0", success=False)
        self.assertEqual(self.manifest.read_bytes(), original)
        self.assertEqual(len(self.log.read_text().splitlines()), 1)

    def test_local_uploads_exact_bytes_and_records_only_selected_target(self):
        objects = {}

        class Handler(BaseHTTPRequestHandler):
            def do_PUT(self):
                objects[self.path] = self.rfile.read(int(self.headers.get("Content-Length", "0")))
                self.send_response(200)
                self.end_headers()

            def log_message(self, *args):
                pass

        server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        self.addCleanup(server.server_close)
        self.addCleanup(thread.join)
        self.addCleanup(server.shutdown)
        self.env["TTREK_LOCAL_S3_ENDPOINT"] = f"http://127.0.0.1:{server.server_port}"
        checksum = self.binary("macos-arm64")
        self.binary("linux-arm64-musl", b"not selected")
        self.run_script("upload.sh", "local", "1.0.0", "macos-arm64")
        self.assertEqual(objects, {"/ttrek-local": b"",
            "/ttrek-local/ttrek/1.0.0/macos-arm64/ttrek": b"release\x00\xff\n"})
        self.assertEqual(json.loads(self.manifest.read_text()),
            {"local": {"1.0.0": {"macos-arm64": checksum}}})
        self.assertEqual((self.root / "seed/ttrek-local/ttrek/1.0.0/macos-arm64/ttrek").read_bytes(),
            b"release\x00\xff\n")
        self.assertFalse(self.log.exists())


if __name__ == "__main__":
    unittest.main()
