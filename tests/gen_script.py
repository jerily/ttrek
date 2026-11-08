"""Exercise the real CLI, resolver and generated scripts with a fixture registry."""
import base64
import copy
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import tarfile
import tempfile

BINARY = Path(sys.argv[1]).resolve()
CMAKE = Path(sys.argv[2]).resolve()
CURL = Path(sys.argv[3]).absolute()


def run(args, **kwargs):
    return subprocess.run(args, capture_output=True, text=True, timeout=90, **kwargs)


with tempfile.TemporaryDirectory(prefix="ttrek gen-script ' ") as temporary:
    root = Path(temporary)
    home = root / "home"
    home.mkdir()
    tools = root / "bin"
    tools.mkdir()
    (tools / "cmake").symlink_to(CMAKE)
    (tools / "curl").symlink_to(CURL)
    project = root / "project"
    project.mkdir()
    registry_file = root / "registry.json"
    # Keep host registry/curl settings out of the fixture, and retain logs in
    # captured output before TemporaryDirectory removes the failed build.
    env = dict(os.environ, HOME=str(home), CURL_HOME=str(home), XDG_CONFIG_HOME=str(home / ".config"),
               TTREK_TEST_REGISTRY=str(registry_file), TTREK_NO_UPDATE_CHECK="1",
               TTREK_FAIL_VERBOSE="1", TTREK_MAKE_THREADS="2",
               PATH=str(tools) + os.pathsep + os.environ["PATH"])
    spec = {"name": "demo", "version": "1.0.0", "dependencies": {"app": "1.0.0"},
            "scripts": {"preInstall": "printf pre > pre-hook", "postInstall": "printf post > post-hook"}}
    spec_file = project / "ttrek.json"
    spec_file.write_text(json.dumps(spec))

    registry = {}
    for name, dependencies in (("base", {}), ("app", {"base": "1.0.0"})):
        source = root / name
        source.mkdir()
        (source / "value.c").write_text("int value(void) { return 41; }\n")
        # Exercise C++ standard headers too: a compiler/SDK mismatch on macOS
        # can compile C successfully while failing on libc++ headers.
        (source / "sdk_probe.cpp").write_text(
            "#include <algorithm>\n#include <string>\n"
            "std::string sdk_probe() { std::string s = \"sdk\"; "
            "std::sort(s.begin(), s.end()); return s; }\n")
        (source / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.22)\nproject(" + name + " C CXX)\n"
            "option(BUILD_SHARED_LIBS \"Shared library\" ON)\n"
            + ("if(DEFINED ENV{DEPENDENCY_ONLY})\nmessage(FATAL_ERROR \"Environment leaked\")\nendif()\n"
               "if(NOT EXISTS \"${CMAKE_INSTALL_PREFIX}/include/base.h\")\n"
               "message(FATAL_ERROR \"Dependency not installed first\")\nendif()\n" if name == "app" else "")
            + "add_library(" + name + " value.c sdk_probe.cpp)\n"
            "install(TARGETS " + name + " LIBRARY DESTINATION lib ARCHIVE DESTINATION lib)\n"
            "install(FILES value.c DESTINATION include RENAME " + name + ".h)\n")
        archive = root / (name + ".tar.gz")
        with tarfile.open(archive, "w:gz") as output:
            output.add(source, arcname=name)
        steps = [{"cmd": "download", "url": archive.as_uri()}, {"cmd": "unpack"}]
        patches = {}
        if name == "base":
            patch = "--- a/value.c\n+++ b/value.c\n@@ -1 +1 @@\n-int value(void) { return 41; }\n+int value(void) { return 42; } /* __TTREK_PATCH_EOF__ */\n"
            patches["value.diff"] = base64.b64encode(patch.encode()).decode()
            steps += [{"cmd": "patch", "filename": "value.diff", "p_num": "1"},
                      {"cmd": "env_variable", "op": "set", "name": "DEPENDENCY_ONLY", "value": "yes"}]
        steps += [{"cmd": "cd"}, {"cmd": "cmake_config", "options": [
            {"name": "BUILD_SHARED_LIBS", "value": "ON"},
            {"name": "BUILD_SHARED_LIBS", "value": "OFF", "if": "+static"}]},
                  {"cmd": "make", "parallel": True}, {"cmd": "make_install"}]
        registry["/registry/" + name] = {"1.0.0": dependencies}
        registry[f"/registry/{name}/1.0.0/{platform.system()}/{platform.machine()}"] = {
            "version": "1.0.0", "dependencies": dependencies,
            "install_script": steps, "patches": patches}
    registry_file.write_text(json.dumps(registry))

    def generate(*args, ok=True):
        before = {p.relative_to(project): p.read_bytes() for p in project.rglob("*") if p.is_file()}
        directories = sorted(str(p.relative_to(project)) for p in project.rglob("*"))
        result = run([BINARY, "gen-script", *args], cwd=project, env=env)
        assert (result.returncode == 0) == ok, (args, result.stdout, result.stderr)
        assert before == {p.relative_to(project): p.read_bytes() for p in project.rglob("*") if p.is_file()}
        assert directories == sorted(str(p.relative_to(project)) for p in project.rglob("*"))
        if ok:
            assert result.stdout.startswith("#!"), result.stdout
            assert str(BINARY) not in result.stdout
        else:
            assert not result.stdout, result.stdout
            assert "error:" in result.stderr
        return result

    for shell, static in (("bash", False), ("sh", True)):
        args = [] if shell == "bash" else ["-shell", shell]
        if static:
            args += ["-static"]
        generated = generate(*args).stdout
        assert "'BUILD_SHARED_LIBS'=\"ON\"" in generated
        assert ("'BUILD_SHARED_LIBS'=\"OFF\"" in generated) == static
        output = root / (shell + " build")
        output.mkdir()
        script = output / "build.sh"
        script.write_text(generated)
        assert run([shell, "-n", script]).returncode == 0
        executed = run([shell, script], cwd=root, env=env)
        assert executed.returncode == 0, (executed.stdout, executed.stderr)
        suffix = ".a" if static else (".dylib" if platform.system() == "Darwin" else ".so")
        assert (output / "build/install/lib" / ("libapp" + suffix)).exists()
        assert "return 42" in (output / "build/install/include/base.h").read_text()
        assert (output / "pre-hook").read_text() == "pre"
        assert (output / "post-hook").read_text() == "post"
        # A generated script also supports rerunning after a completed build.
        repeated = run([shell, script], cwd=root, env=env)
        assert repeated.returncode == 0, (repeated.stdout, repeated.stderr)

    generate("-shell", "fish", ok=False)
    generate("-shell", ok=False)
    generate("-bogus", ok=False)
    generate("-strategy", "invalid", ok=False)
    generate("missing", ok=False)
    generate("app@9.0.0", ok=False)
    app_path = f"/registry/app/1.0.0/{platform.system()}/{platform.machine()}"
    broken = copy.deepcopy(registry)
    broken[app_path]["install_script"] = [{"cmd": "unknown"}]
    registry_file.write_text(json.dumps(broken))
    generate(ok=False)
    broken[app_path]["install_script"] = "not an array"
    registry_file.write_text(json.dumps(broken))
    generate(ok=False)
    registry_file.write_text(json.dumps(registry))
    spec["useFlags"] = ["+static"]
    spec_file.write_text(json.dumps(spec))
    assert "'BUILD_SHARED_LIBS'=\"OFF\"" in generate().stdout
    spec["useFlags"] = ["-static"]
    spec_file.write_text(json.dumps(spec))
    assert "'BUILD_SHARED_LIBS'=\"OFF\"" not in generate().stdout
    assert "'BUILD_SHARED_LIBS'=\"OFF\"" in generate("-static").stdout
    spec["useFlags"] = [42]
    spec_file.write_text(json.dumps(spec))
    generate(ok=False)
    spec["useFlags"] = []
    spec["dependencies"] = {"app": 42}
    spec_file.write_text(json.dumps(spec))
    generate(ok=False)
    spec["dependencies"] = {}
    spec["scripts"] = {}
    spec_file.write_text(json.dumps(spec))
    generate()

    # A lock influences version selection, but never removes packages from the script.
    for name in ("base", "app"):
        dependencies = {} if name == "base" else {"base": "1.1.0"}
        registry["/registry/" + name]["1.1.0"] = dependencies
        old_path = f"/registry/{name}/1.0.0/{platform.system()}/{platform.machine()}"
        new_path = f"/registry/{name}/1.1.0/{platform.system()}/{platform.machine()}"
        registry[new_path] = copy.deepcopy(registry[old_path])
        registry[new_path]["version"] = "1.1.0"
        registry[new_path]["dependencies"] = dependencies
    registry_file.write_text(json.dumps(registry))
    spec["dependencies"] = {"app": ">=1.0.0"}
    spec_file.write_text(json.dumps(spec))
    (project / "ttrek-lock.json").write_text(json.dumps({"packages": {
        "app": {"version": "1.0.0"}, "base": {"version": "1.0.0"}}}))
    assert "VERSION='1.0.0'" in generate().stdout
    assert "VERSION='1.1.0'" not in generate("-strategy", "locked").stdout
    assert "VERSION='1.1.0'" in generate("-strategy", "latest").stdout

    # Build the local project from its portable script directory after dependencies.
    spec["build"] = {"default": [
        {"cmd": "download", "url": "file:///must-not-download-local-project"},
        {"cmd": "cd"}, {"cmd": "cmake_config"}, {"cmd": "make"}, {"cmd": "make_install"}]}
    spec_file.write_text(json.dumps(spec))
    (project / "value.c").write_text("int local_value(void) { return 7; }\n")
    (project / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.22)\nproject(local C)\n"
        "add_library(local STATIC value.c)\ninstall(TARGETS local ARCHIVE DESTINATION lib)\n")
    generated = generate("-shell", "sh").stdout
    assert "must-not-download" not in generated
    assert str(project) not in generated
    assert "PACKAGE='demo'" not in generate("app@1.0.0").stdout
    script = project / "build.sh"
    script.write_text(generated)
    result = run(["sh", script], cwd=root, env=env)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert (project / "build/install/lib/liblocal.a").exists()

    obsolete = run([BINARY, "install", "-bootstrap"], cwd=project, env=env)
    assert obsolete.returncode != 0 and "unknown install option: -bootstrap" in obsolete.stderr
print("gen-script CLI, static/shared builds, patches, isolation and error checks passed")
