"""Activation uses run's environment and preserves Bash terminal behavior."""
import errno
import json
import os
from pathlib import Path
import pty
import select
import shlex
import signal
import subprocess
import sys
import tempfile
import time

binary = Path(sys.argv[1]).resolve()
names = ["PATH", "LD_LIBRARY_PATH", "PROJECT_HOME", "PROJECT_INSTALL_DIR",
         "TTREK_VENV", "TTREK_VENV_PROMPT", "PS1", "VIRTUAL_ENV", "PYTHONHOME"]
probe = ("import json,os; print(json.dumps({k:os.environ.get(k) for k in "
         + repr(names + ["PWD"]) + "}))")
command = shlex.join([sys.executable, "-c", probe])

with tempfile.TemporaryDirectory(prefix="ttrek activate ' $(touch injected) ") as directory:
    root = Path(directory).resolve()
    home, project, registry = root / "home", root / "project", root / "registry"
    for path in (home, project, registry):
        path.mkdir()
    child = project / "nested"
    child.mkdir()
    env = dict(os.environ, HOME=str(home), TTREK_NO_UPDATE_CHECK="1", TTREK_TELEMETRY="0",
               LD_LIBRARY_PATH="", PROJECT_HOME="old ' $(touch injected)",
               VIRTUAL_ENV="python-environment", TERM="dumb")
    for name in ("SHELL", "PROJECT_INSTALL_DIR", "TTREK_VENV", "TTREK_VENV_PROMPT",
                 "TTREK_VENV_DISABLE_PROMPT", "PYTHONHOME", "PS1", "PROMPT_COMMAND"):
        env.pop(name, None)
    (home / ".bashrc").write_text("PS1='base> '\nexport PS1\nset -u\n"
                                  "alias activation_alias='printf alias-ok'\n")

    def run(*args, cwd=child, input=None, environment=env, ok=True):
        result = subprocess.run([binary, *args], cwd=cwd, env=environment, input=input,
                                capture_output=True, text=True, timeout=15)
        assert (result.returncode == 0) == ok, (args, result.stdout, result.stderr)
        return result

    run("init", "-y", "-registry", str(registry), cwd=project)
    prefix = project / "ttrek-venv/local"
    (prefix / "bin").mkdir()
    tool = prefix / "bin/activation-tool"
    tool.write_text("#!/bin/sh\nprintf tool-ok\\\\n\n")
    tool.chmod(0o755)

    baseline = json.loads(run("run", "--", sys.executable, "-c", probe).stdout)
    result = run("activate", input=command + "\nactivation-tool\nactivation_alias\n"
                 "printf '\\n'\ndeactivate\n" + command + "\n"
                 "declare -F deactivate && exit 99\nexit 0\n")
    rows = result.stdout.splitlines()
    active, restored = json.loads(rows[0]), json.loads(rows[-1])
    assert rows[1:3] == ["tool-ok", "alias-ok"], rows
    for name in names[:4]:
        assert active[name] == baseline[name], (name, active, baseline)
    assert active["PWD"] == str(child)
    assert active["PS1"] == "(ttrek) base> "
    assert active["TTREK_VENV"] == str(project / "ttrek-venv")
    assert active["TTREK_VENV_PROMPT"] == "ttrek"
    for name in names:
        assert restored[name] == ("base> " if name == "PS1" else env.get(name)), (name, restored)
    assert active["VIRTUAL_ENV"] == env["VIRTUAL_ENV"]
    assert not (child / "injected").exists()

    # Empty values, prompt opt-out, and preexisting activation markers survive.
    alternate = dict(env, TTREK_VENV_DISABLE_PROMPT="1", TTREK_VENV="outer",
                     TTREK_VENV_PROMPT="", PROJECT_INSTALL_DIR="", LD_LIBRARY_PATH="old-libs")
    rows = run("activate", environment=alternate,
               input=command + "\ndeactivate\n" + command + "\nexit\n").stdout.splitlines()
    assert json.loads(rows[0])["PS1"] == "base> "
    restored = json.loads(rows[1])
    for name in names:
        assert restored[name] == ("base> " if name == "PS1" else alternate.get(name)), (name, restored)

    # No inherited search paths and no user startup file are valid too.
    (home / ".bashrc").rename(home / "saved-bashrc")
    minimal = dict(env)
    for name in ("PATH", "LD_LIBRARY_PATH", "PROJECT_HOME"):
        minimal.pop(name, None)
    rows = run("activate", environment=minimal,
               input=command + "\ndeactivate\n" + command + "\nexit\n").stdout.splitlines()
    active, restored = map(json.loads, rows)
    assert active["PATH"] == str(prefix / "bin"), active
    assert active["PS1"].startswith("(ttrek) "), active
    assert restored["LD_LIBRARY_PATH"] is None and restored["PROJECT_HOME"] is None, restored
    assert restored["PS1"] == active["PS1"][len("(ttrek) "):], restored
    (home / "saved-bashrc").rename(home / ".bashrc")
    python_home = run("activate", environment=dict(env, PYTHONHOME="python-home"),
                      input="printf '%s\\n' \"$PYTHONHOME\"\nexit\n")
    assert python_home.stdout == "python-home\n", python_home
    run("activate", input="")  # EOF returns to the parent shell.

    (home / ".ttrek").mkdir()
    run("init", "-y", "-registry", str(registry), cwd=home / ".ttrek")
    user = json.loads(run("activate", "-u", cwd=root, input=command + "\nexit\n").stdout)
    assert user["PROJECT_HOME"] == str(home / ".ttrek")
    assert user["PROJECT_INSTALL_DIR"] == str(home / ".ttrek/ttrek-venv/local")
    assert user["PWD"] == str(root)
    for args in (("-u", "-g"), ("unexpected",), ("-unknown",)):
        run("activate", *args, ok=False)
    run("activate", cwd=root, ok=False)
    failed = run("activate", input="exit 7\n", ok=False)
    assert failed.returncode == 7, failed
    missing = run("activate", environment=dict(env, PATH=str(root)), ok=False)
    assert "cannot start bash" in missing.stderr, missing

    # A real controlling terminal catches pipeline-based launches that break
    # job control, Ctrl-C, and interactive prompts.
    pid, fd = pty.fork()
    if pid == 0:
        os.chdir(child)
        os.execve(binary, [str(binary), "activate"], env)
    output = b""

    def expect(token):
        global output
        deadline = time.monotonic() + 10
        while token not in output:
            assert time.monotonic() < deadline, output
            if select.select([fd], [], [], 0.1)[0]:
                try:
                    chunk = os.read(fd, 65536)
                except OSError as error:
                    if error.errno == errno.EIO:
                        chunk = b""
                    else:
                        raise
                assert chunk, output
                output += chunk
        consumed, output = output.split(token, 1)
        return consumed

    try:
        startup = expect(b"(ttrek) base> ")
        assert b"no job control" not in startup, startup
        os.write(fd, b"[[ -t 0 && -t 1 && -t 2 && $- == *m* ]] && printf 'terminal-%s\\n' ok\n")
        expect(b"terminal-ok\r\n")
        expect(b"(ttrek) base> ")
        os.write(fd, b"sleep 30\n")
        time.sleep(0.2)
        os.write(fd, b"\x03")
        expect(b"(ttrek) base> ")
        os.write(fd, b"deactivate\n")
        expect(b"\r\nbase> ")
        os.write(fd, b"exit 7\n")
        waited, status = os.waitpid(pid, 0)
        assert waited == pid and os.waitstatus_to_exitcode(status) == 7, status
        pid = None
    finally:
        if pid is not None:
            os.kill(pid, signal.SIGKILL)
            os.waitpid(pid, 0)
        os.close(fd)

print("Activation environment, restoration, modes, errors and terminal behavior passed")
