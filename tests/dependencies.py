"""Runtime preflight must fail before hooks, downloads or environment changes."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='ttrek-dependencies-') as temporary:
    root = Path(temporary)
    tools = root / 'tools'
    tools.mkdir()
    project = root / 'project'
    project.mkdir()
    spec = project / 'ttrek.json'
    spec.write_text(json.dumps({'dependencies': {}, 'scripts': {'preInstall': 'touch hook-ran'}}))
    before = spec.read_bytes()
    env = dict(os.environ, PATH=str(tools), HOME=str(root), TTREK_NO_UPDATE_CHECK='1')
    env.pop('CC', None)
    env.pop('CXX', None)

    def run(*args):
        return subprocess.run([binary, *args], cwd=project, env=env,
                              capture_output=True, text=True, timeout=10)

    for command in ('install', 'update'):
        result = run(command, '-y')
        assert result.returncode != 0, result
        for word in ('missing external build dependencies', 'cmake', 'make', 'gcc',
                     'sudo apt install build-essential', 'sudo snap install cmake --classic'):
            assert word in result.stderr, result.stderr
        assert spec.read_bytes() == before
        assert sorted(p.name for p in project.iterdir()) == ['ttrek.json']
    assert run('help', 'install').returncode == 0
    assert 'unknown install option' in run('install', '-bogus').stderr
    assert 'missing external' not in run('install', '-bogus').stderr

    # Isolate one missing dependency and honor explicit compiler overrides.
    names = ('bash', 'cmake', 'make', 'curl', 'git', 'patch', 'pkg-config',
             'perl', 'tar', 'gzip', 'cc', 'c++')
    stub = root / 'tool-stub'
    stub.write_text('#!/bin/sh\nexit 0\n')
    stub.chmod(0o755)
    for name in names:
        (tools / name).symlink_to(stub)
    (tools / 'cmake').unlink()
    result = run('install', '-y')
    assert 'missing external build dependencies: cmake\n' in result.stderr, result.stderr
    (tools / 'cmake').symlink_to(stub)
    env['CC'] = 'missing-cc -O2'
    env['CXX'] = 'missing-cxx'
    result = run('update', '-y')
    assert 'CC=missing-cc CXX=missing-cxx\n' in result.stderr, result.stderr
    assert sorted(p.name for p in project.iterdir()) == ['ttrek.json']

    check = Path(__file__).resolve().parents[1] / 'src/check_dependencies.sh'
    env['CC'] = str(tools / 'cc') + ' -O2'
    env['CXX'] = str(tools / 'c++')
    subprocess.run(['/bin/sh', check], env=env, check=True)
    env.pop('CC')
    env.pop('CXX')
    (tools / 'cc').rename(tools / 'clang')
    (tools / 'c++').rename(tools / 'clang++')
    subprocess.run(['/bin/sh', check], env=env, check=True)
print('External dependency preflight passed')
