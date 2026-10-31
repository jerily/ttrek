"""Optional live test: run with ttrek.sh listening on localhost:8080."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="ttrek-registry-test-") as directory:
    project = Path(directory)
    spec = {"name": "solver-test", "version": "1.0.0", "dependencies": {}, "useFlags": []}
    (project / "ttrek.json").write_text(json.dumps(spec))
    original = (project / "ttrek.json").read_bytes()
    cases = [
        (["pretend", "tjson@1.0.26", "twebserver@1.47.54"], True),
        (["pretend", "tjson@1.0.26", "tcl@8.6.15"], True),
        (["pretend", "tjson@1.0.26", "tcl@8.6.13"], False),
        (["pretend", "does-not-exist-solver-test"], False),
        (["pretend", "-strategy", "invalid"], False),
    ]
    for args, succeeds in cases:
        result = subprocess.run([binary, *args], cwd=project, capture_output=True, text=True, timeout=30)
        assert (result.returncode == 0) == succeeds, (args, result.stdout, result.stderr)
        if succeeds:
            assert "tjson@1.0.26" in result.stdout
            assert result.stdout.index("zlib@") < result.stdout.index("tcl@") < result.stdout.index("tjson@")
        else:
            assert "error:" in result.stderr
        assert (project / "ttrek.json").read_bytes() == original
        assert sorted(path.name for path in project.iterdir()) == ["ttrek.json"]
print("Live registry resolution and read-only checks passed")
