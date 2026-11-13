"""Shared source reuse, isolation, failures and locking in both script paths."""
import copy
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tarfile
import tempfile
import time
import zipfile

BINARY, CMAKE, CURL = (Path(arg).absolute() for arg in sys.argv[1:])


def run(args, *, ok=True, **kwargs):
    result = subprocess.run(args, capture_output=True, text=True, timeout=90, **kwargs)
    assert (result.returncode == 0) == ok, (args, result.stdout, result.stderr)
    return result


def wait_for(path, process):
    deadline = time.monotonic() + 15
    while not path.exists():
        assert process.poll() is None, process.communicate()
        assert time.monotonic() < deadline, path
        time.sleep(0.02)


with tempfile.TemporaryDirectory(prefix="ttrek shared ' ") as temporary:
    root = Path(temporary).resolve()
    home = root / 'home'
    home.mkdir()
    tools = root / 'bin'
    tools.mkdir()
    (tools / 'cmake').symlink_to(CMAKE)
    (tools / 'curl').symlink_to(CURL)
    env = dict(os.environ, HOME=str(home), CURL_HOME=str(home),
               XDG_CONFIG_HOME=str(home / '.config'), TTREK_NO_UPDATE_CHECK='1',
               TTREK_FAIL_VERBOSE='1', SHELL='/bin/sh', IS_TTY='0',
               PATH=str(tools) + os.pathsep + os.environ['PATH'])
    source = root / 'source'
    source.mkdir()
    (source / 'value.txt').write_text('original source\n')
    (source / 'CMakeLists.txt').write_text(
        'cmake_minimum_required(VERSION 3.22)\nproject(shared NONE)\n'
        'file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/source-path.txt" "${CMAKE_CURRENT_SOURCE_DIR}")\n'
        'configure_file(value.txt "${SERVICE}.txt" COPYONLY)\n'
        'install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${SERVICE}.txt" DESTINATION share)\n')
    archives = {}
    for extension in ('tar.gz', 'zip'):
        archive = root / ('sdk.' + extension)
        if extension == 'tar.gz':
            with tarfile.open(archive, 'w:gz') as output:
                output.add(source, arcname='sdk')
        else:
            with zipfile.ZipFile(archive, 'w') as output:
                for path in source.iterdir():
                    output.write(path, arcname='sdk/' + path.name)
        archives[extension] = archive
    registry = root / 'registry'
    recipes = {}
    for name in ('one', 'two', 'private'):
        steps = [
            {'cmd': 'download', 'url': archives['tar.gz'].as_uri(), 'filename': 'sdk.tar.gz'},
            {'cmd': 'unpack', 'filename': 'sdk.tar.gz'}, {'cmd': 'cd'},
            {'cmd': 'cmake_config', 'options': [{'name': 'SERVICE', 'value': name}]},
            {'cmd': 'cmake_install'},
        ]
        if name != 'private':
            steps[1]['shared'] = 'sdk-1.0'
        recipes[name] = {'name': name, 'version': '1.0.0', 'dependencies': {}, 'build': {'default': steps}}

    def save_recipes():
        for name, recipe in recipes.items():
            directory = registry / name / '1.0.0'
            directory.mkdir(parents=True, exist_ok=True)
            (directory / 'ttrek.json').write_text(json.dumps(recipe))

    save_recipes()
    for mode in ('bash', 'sh', 'native'):
        project = root / mode
        project.mkdir()
        cli = lambda *args, **kwargs: run([BINARY, *args], cwd=project, env=env, **kwargs)
        cli('init', '-y', '-registry', str(registry))
        build = project / ('ttrek-venv/build' if mode == 'native' else 'build')
        prefix = project / ('ttrek-venv/local' if mode == 'native' else 'build/install')
        scripts = {}
        for name in recipes:
            if mode == 'native':
                cli('install', '-y', name + '@1.0.0')
                script = build / ('install-' + name + '-1.0.0.sh')
            else:
                script = project / (name + '.sh')
                script.write_text(cli('gen-script', '-shell', mode, name + '@1.0.0').stdout)
                run([mode, script], env=env)
            scripts[name] = script
            assert (prefix / 'share' / (name + '.txt')).read_text() == 'original source\n'
        shared = Path((build / 'build/one-1.0.0/source-path.txt').read_text())
        assert shared == Path((build / 'build/two-1.0.0/source-path.txt').read_text())
        assert shared.is_relative_to(build / 'shared-source/sdk-1.0')
        assert list((build / 'shared-source').rglob('.complete')) == [shared.parent / '.complete']
        original_inode = (shared / 'value.txt').stat().st_ino
        for path in [shared, *shared.rglob('*')]:
            assert path.stat().st_mode & 0o222 == 0, path
        if os.geteuid() != 0:
            try:
                (shared / 'value.txt').write_text('must fail')
                raise AssertionError('shared source was writable')
            except PermissionError:
                pass
        private = build / 'source/private-1.0.0'
        (private / 'stale.txt').touch()
        # Rebuilding either service leaves the other package's build untouched.
        sentinel = build / 'build/two-1.0.0/keep.txt'
        sentinel.write_text('keep')
        run(['bash' if mode == 'native' else mode, scripts['one']], env=env)
        assert sentinel.read_text() == 'keep'
        assert (shared / 'value.txt').stat().st_ino == original_inode
        run(['bash' if mode == 'native' else mode, scripts['private']], env=env)
        assert not (private / 'stale.txt').exists()
        # Concurrent package scripts also download the same archive pathname.
        processes = [subprocess.Popen(['bash' if mode == 'native' else mode, scripts[name]],
                     env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                     for name in ('one', 'two')]
        for process in processes:
            output = process.communicate(timeout=60)
            assert process.returncode == 0, output
        assert (shared / 'value.txt').stat().st_ino == original_inode
        assert not list((build / 'download').glob('sdk.tar.gz.??????'))

    # Invalid combinations fail in the shared generator used by both paths.
    valid = copy.deepcopy(recipes['one'])
    invalid = []
    for key in (None, True, '', '.', '..', '../sdk', '/sdk', 'two words', '$(bad)', 'a' * 129):
        recipe = copy.deepcopy(valid)
        recipe['build']['default'][1]['shared'] = key
        invalid.append(recipe)
    for command in ({'cmd': 'patch', 'filename': 'unused.diff'}, {'cmd': 'git', 'url': 'unused'},
                    {'cmd': 'autogen'}, {'cmd': 'unpack'},
                    {'cmd': 'unpack', 'shared': 'another-key'},
                    {'cmd': 'env_variable', 'op': 'set', 'name': 'SOURCE_DIR', 'value': '/tmp'}):
        recipe = copy.deepcopy(valid)
        recipe['build']['default'].insert(0, command)
        invalid.append(recipe)
    recipe = copy.deepcopy(valid)
    recipe['build']['default'][1]['destination'] = 'nested'
    invalid.append(recipe)
    recipe = copy.deepcopy(valid)
    recipe['build']['default'][1]['cmd'] = 'unpac'
    recipe['build']['default'].append({'cmd': 'pat', 'filename': 'unused.diff'})
    invalid.append(recipe)
    for recipe in invalid:
        recipes['one'] = recipe
        save_recipes()
        result = cli('gen-script', 'one@1.0.0', ok=False)
        assert 'shared' in result.stderr.lower(), result.stderr
        assert not result.stdout
    # Disabled mutations are permitted, and local project unpack is skipped.
    recipes['one'] = copy.deepcopy(valid)
    recipes['one']['build']['default'].insert(0, {'cmd': 'autogen', 'if': '+unused'})
    save_recipes()
    cli('gen-script', 'one@1.0.0')
    local = json.loads((project / 'ttrek.json').read_text())
    local['build'] = {'default': [{'cmd': 'unpack', 'shared': None}, {'cmd': 'autogen', 'if': '+unused'}]}
    (project / 'ttrek.json').write_text(json.dumps(local))
    cli('gen-script')

    # Exercise the embedded helper directly with a controllable extractor. This
    # makes a SIGKILL during extraction and concurrent first use deterministic.
    generated = cli('gen-script', 'one@1.0.0').stdout
    helper = root / 'helper.sh'
    helper.write_text(generated[generated.index('shared_archive_hash()'):generated.index('\n(\n', generated.index('shared_unpack()'))]
                      + '\nROOT_BUILD_DIR=$1\nshift\nshared_unpack "$@"\n')
    extractor = root / 'extractor.py'
    extractor.write_text('#!' + sys.executable + '\n'
        'import os, pathlib, subprocess, sys, time\n'
        'with open(os.environ["EXTRACTIONS"], "a") as out: out.write("extract\\n")\n'
        'ready = pathlib.Path(os.environ["READY"])\n'
        'ready.touch()\n'
        'while pathlib.Path(os.environ["WAIT"]).exists(): time.sleep(0.02)\n'
        'if os.environ.get("FAIL"):\n'
        '    (pathlib.Path(sys.argv[2]) / "partial.txt").write_text("partial extraction")\n'
        '    sys.exit(7)\n'
        'sys.exit(subprocess.call([os.environ["BINARY"], "unpack", *sys.argv[1:]]))\n')
    extractor.chmod(0o755)
    helper_env = dict(env, EXTRACTIONS=str(root / 'extractions'), READY=str(root / 'ready'),
                      WAIT=str(root / 'wait'), BINARY=str(BINARY))
    ready, wait = root / 'ready', root / 'wait'
    count = root / 'extractions'
    # Exercise the macOS fallback tools on Linux too: hide flock and sha256sum.
    fallback_bin = root / 'fallback-bin'
    fallback_bin.mkdir()
    for command in ('perl', 'shasum', 'mkdir', 'cp', 'find', 'chmod', 'rm', 'touch', 'mv'):
        (fallback_bin / command).symlink_to(shutil.which(command))
    for fallback in (False, True):
        preparation_env = dict(helper_env)
        if fallback:
            preparation_env['PATH'] = str(fallback_bin)
        for extension, archive in archives.items():
            cache_root = root / ('helper-' + str(fallback) + extension)
            command = ['/bin/sh', helper, cache_root, archive, 'sdk', extractor]
            count.write_text('')
            ready.unlink(missing_ok=True)
            # A failing extractor must not produce a reusable entry.
            run(command, env=dict(preparation_env, FAIL='1'), ok=False)
            assert not list(cache_root.rglob('.complete'))
            assert not list(cache_root.rglob('*.tmp'))
            # Kill the entire preparing process group while unpack is active.
            wait.touch()
            ready.unlink()
            process = subprocess.Popen(command, env=preparation_env, start_new_session=True,
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            try:
                wait_for(ready, process)
            finally:
                os.killpg(process.pid, signal.SIGKILL)
                process.communicate(timeout=10)
            assert list(cache_root.rglob('*.tmp'))
            assert not list(cache_root.rglob('.complete'))
            # Two first users race to recover it; only one may extract.
            count.write_text('')
            ready.unlink()
            first = subprocess.Popen(command, env=preparation_env, stdout=subprocess.PIPE,
                                     stderr=subprocess.PIPE, text=True)
            second = None
            try:
                wait_for(ready, first)
                second = subprocess.Popen(command, env=preparation_env, stdout=subprocess.PIPE,
                                          stderr=subprocess.PIPE, text=True)
                time.sleep(0.3)
                assert count.read_text() == 'extract\n'
            finally:
                wait.unlink()
                outputs = [p.communicate(timeout=30) for p in (first, second) if p]
            assert first.returncode == second.returncode == 0, outputs
            assert outputs[0][0] == outputs[1][0], outputs
            assert count.read_text() == 'extract\n'
            shared = Path(outputs[0][0].strip())
            assert (shared / 'value.txt').read_text() == 'original source\n'
            assert not list(cache_root.rglob('*.tmp'))
            assert run(command, env=preparation_env).stdout.strip() == str(shared)
            assert count.read_text() == 'extract\n'
            # The same key with different bytes must select a new entry.
            alternate = root / ('changed-' + str(fallback) + extension + '.tar.gz')
            (source / 'value.txt').write_text('changed source\n')
            with tarfile.open(alternate, 'w:gz') as output:
                output.add(source, arcname='sdk')
            changed = run(['/bin/sh', helper, cache_root, alternate, 'sdk', extractor], env=preparation_env)
            assert Path(changed.stdout.strip()) != shared
            assert (Path(changed.stdout.strip()) / 'value.txt').read_text() == 'changed source\n'
            assert (shared / 'value.txt').read_text() == 'original source\n'
            assert len(list(cache_root.rglob('.complete'))) == 2
print('shared sources: native/Bash/sh, reuse, isolation, validation, failures, SIGKILL, concurrency and fallbacks passed')
