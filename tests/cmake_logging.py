"""Preserve CMake selection, redirected diagnostics and command exit status."""
import os
from pathlib import Path
import subprocess
import tempfile

helper = Path(__file__).resolve().parents[1] / "src/install_common_static.sh"

with tempfile.TemporaryDirectory(prefix="ttrek-cmake-logging-") as temporary:
    root = Path(temporary)
    tools = root / "bin"
    tools.mkdir()
    custom = tools / "cmake"
    custom.write_text('#!/bin/sh\nprintf "%s\\n" "$@"\necho custom-error >&2\nexit 7\n')
    custom.chmod(0o755)
    log = root / "command.log"
    script = '''
IS_TTY=0
. "$1"
shift
status=0
cmd "$@" > command.log 2>&1 || status=$?
printf '%s\n' "$LATEST_COMMAND"
exit "$status"
'''

    def run(shell, path, *args):
        return subprocess.run([shell, "-c", script, "test", str(helper), "cmake", *args],
                              cwd=root, env=dict(os.environ, PATH=path),
                              capture_output=True, text=True, timeout=30)

    for shell in ("bash", "sh"):
        # A caller-selected CMake and its exact arguments must be preserved.
        result = run(shell, str(tools) + os.pathsep + os.environ["PATH"], "two words", "")
        assert result.returncode == 7, result
        assert log.read_text() == "two words\n\ncustom-error\n"
        assert result.stdout == "cmake two words \n", result

        # Exercise the real Snap regression when that installation is available.
        if Path("/snap/bin/cmake").exists() and Path("/snap/cmake/current/bin/cmake").is_file():
            path = "/snap/bin" + os.pathsep + os.environ["PATH"]
            result = run(shell, path, "--version")
            assert result.returncode == 0, result
            assert "cmake version" in log.read_text(), log.read_text()
            result = run(shell, path, "-P", "missing.cmake")
            assert result.returncode != 0, result
            assert "CMake Error" in log.read_text(), log.read_text()
            assert result.stdout == "cmake -P missing.cmake\n", result

print("CMake selection, logging and failure status passed for bash and sh")
