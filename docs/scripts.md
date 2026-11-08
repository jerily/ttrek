# Run scripts and generate builds

[Documentation](readme.md) · [Project overview](../readme.md)

Use named scripts to run commands supplied by a project or an installed package.
You can also generate a build script that runs without ttrek.

## Run installed package scripts

Recipes can define a `scripts` object mapping names to shell script strings or
file references, for example `"scripts": {"demo": {"file": "demo.sh"}}`.
Referenced UTF-8 files must be regular files directly in the version's recipe
directory; paths and symbolic links are rejected. Local and hosted registries
resolve references to script bodies before installation, so installed scripts
remain available offline. Keep example scripts in files beside `ttrek.json`.
Installation saves the exact version and scripts in
`ttrek-venv/ttrek-manifest.json`, alongside the package's installed file list:

```sh
ttrek install openacs@5.10.1
ttrek scripts -package openacs ls
ttrek scripts -package openacs describe init-local
ttrek run -package openacs -script init-local
ttrek run -package openacs -script start-local
```

Execution and inspection use installed metadata without registry access. Updates
replace cached scripts and uninstall removes them. Packages installed before
this support require `ttrek install -force PACKAGE` using a registry recipe
that supplies the scripts. A plain install of an already installed version does
not refresh its metadata. Use the same `-u` or `-g` mode for all commands when
working with those environments.

Without `-package`, script commands use the project's `ttrek.json` as before.
Package scripts support `ls` and `describe`; `add` and `del` edit project scripts
only. Installing a recipe does not automatically execute its named scripts or
copy them into the project spec.

Scripts run in the installation directory using `$SHELL`, honoring an executable
script's shebang. `PROJECT_HOME` names the project root and `PROJECT_INSTALL_DIR`
names the installation prefix. `PATH` and `LD_LIBRARY_PATH` select that environment.
Keep databases, uploads and other runtime data outside `ttrek-venv`.

Named scripts accept positional arguments after `--`, for example
`ttrek run -package my-app -script start -- --port 3015 --data-dir "my data"`.
Arguments retain spaces, empty strings and shell metacharacters; they are not
evaluated as shell code. Script bodies can read `$1`, `$2`, and so on, or forward
them with `"$@"`. Options after `--` belong to the script, including `-u`, `-g`,
`-package` and `-script`. Use `ttrek run -- COMMAND ARGS...` for the same protection
when invoking a command directly. Arguments to named scripts require `--`.

## Generate a standalone build script

```sh
ttrek gen-script > build.sh
ttrek gen-script -shell sh -static jinja2-tcl > build-static.sh
bash build.sh
```

`gen-script` defaults to Bash, supports `sh`, and replaces `install -bootstrap`.

Recipes can download and unpack an archive in one step:

```json
{"cmd": "fetch", "url": "https://example.com/library-1.0.tar.gz"}
```

For additional source archives, use `destination` relative to `$SOURCE_DIR`
and optionally `archive`, a filename under `$DOWNLOAD_DIR`:

```json
{"cmd": "fetch", "url": "https://example.com/crt-1.0.tar.gz", "archive": "crt-1.0.tar.gz", "destination": "crt/library"}
```

`fetch` creates the destination and strips the archive's top-level directory,
using the same downloader and unpacker as `download`/`unpack`. It leaves
`SOURCE_DIR` and `ARCHIVE_FILE` unchanged. Paths are literal (no shell variable
expansion), and absolute paths and `..` components are rejected. Existing
`download` and `unpack` steps continue to work. Clients without `fetch` support
reject the command; update/rebuild ttrek before using recipes that require it.

A recipe with `"build": {"default": []}` is a dependency-only metapackage.
When an installed bundle changes to such a recipe, ttrek backs up and removes
the bundle's files before building its new dependencies. This lets those
dependencies own the files they install, so removing the umbrella later does
not remove their libraries. Failed migrations restore the original bundle's
files and retain the normal interrupted-install recovery behavior.
It resolves dependencies, embeds patches, and writes only the completed script to
stdout. Generation is read-only; hooks and builds run when you execute the script.
The script installs into `build/install` beside itself and needs no ttrek binary.
It rebuilds packages on each run. Use `-strategy latest|favored|locked` to select versions.

`-static` enables the `+static` USE flag. Recipes use `"if": "+static"` for optional
static build settings; without the flag, their normal defaults apply.
Static Tcl extensions are archives for embedding; an application must register their init functions.

Use `-registry` to select package sources when generating a script; see
[local registries](registries.md#use-a-local-registry).
