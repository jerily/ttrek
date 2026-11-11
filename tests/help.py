"""Every compiled help topic must retain its complete source text."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
docs = Path(__file__).resolve().parents[1] / "docs"
with tempfile.TemporaryDirectory(prefix="ttrek-help-") as directory:
    env = dict(os.environ, HOME=str(Path(directory).resolve()), TTREK_NO_UPDATE_CHECK="1")
    plain = subprocess.run([binary], env=env, cwd=directory, capture_output=True, text=True, timeout=10)
    help_result = subprocess.run([binary, "help"], env=env, cwd=directory,
                                 capture_output=True, text=True, timeout=10)
    assert (plain.returncode, plain.stdout, plain.stderr) == (0, help_result.stdout, help_result.stderr)
    assert not list(Path(directory).iterdir())
    for source in sorted(docs.glob("help_*.txt")):
        topic = source.stem[len("help_"):]
        args = [] if topic == "general" else [topic]
        result = subprocess.run([binary, "help", *args], env=env, cwd=directory,
                                capture_output=True, text=True, timeout=10)
        assert result.returncode == 0, (topic, result.returncode, result.stderr)
        expected = source.read_text(encoding="utf-8")
        if not expected.endswith("\n"):
            expected += "\n"
        assert result.stdout == expected, (topic, result.stdout, result.stderr)
print("All embedded help topics match their documentation")
