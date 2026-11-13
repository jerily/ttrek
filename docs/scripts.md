# Run scripts and generate builds

[Documentation](readme.md) · [Project overview](../readme.md)

Use named scripts to run commands supplied by a project or an installed package.
You can also generate a build script that runs without ttrek.

## Run project scripts

Project scripts can contain inline shell text or reference an existing executable
file, with optional preset arguments:

```json
"scripts": {
  "prepare-linux-amd64-glibc": {
    "file": "bin/prepare-linux.sh",
    "args": ["amd64", "glibc"]
  }
}
```

Paths are relative to the directory containing `ttrek.json`, including when
invoked from a project subdirectory. Files must be regular and executable;
absolute paths, `..` components and symbolic links are rejected. The original
file runs in place, preserving its shebang, `$0` and `BASH_SOURCE` location.
The working directory remains the installation directory.

`args` must be an array of strings. Preset arguments precede any arguments
passed after `--`; both are passed literally without shell expansion.
For example, `ttrek run -script upload-prod -- 1.1.0` supplies the configured
`prod` argument followed by `1.1.0`. File references also work for project
`preInstall` and `postInstall` hooks. `scripts describe` reads the referenced
file, and editing other scripts preserves the reference in `ttrek.json`.

## Run installed package scripts

Recipes can define a `scripts` object mapping names to shell script strings or
file references with optional preset arguments, for example:

```json
"scripts": {
  "demo-csv": {"file": "run-demo.sh", "args": ["csv"]},
  "demo-json": {"file": "run-demo.sh", "args": ["json"]}
}
```

As with project scripts, `args` must be an array of strings; presets precede
runtime arguments and are passed literally. The script's shebang is preserved.
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

Recipes keep downloading and extraction as separate steps:

```json
[
  {"cmd": "download", "url": "https://example.com/crt-v1.0.tar.gz", "filename": "crt-v1.0.tar.gz"},
  {"cmd": "unpack", "filename": "crt-v1.0.tar.gz", "destination": "crt/library"}
]
```

`filename` is relative to `$DOWNLOAD_DIR` and defaults to `$ARCHIVE_FILE`.
`unpack.destination` is relative to `$SOURCE_DIR` and defaults to that directory.
Paths are literal; absolute paths and `..` components are rejected. Parent
folders are created as needed, and extraction strips the top-level directory.
These default forms do not change `SOURCE_DIR` or `ARCHIVE_FILE`.

Packages that build out of tree can explicitly share a pristine source archive:

```json
{"cmd": "unpack", "filename": "aws-sdk-cpp-1.11.924.tar.gz", "shared": "aws-sdk-cpp-1.11.924"}
```

Every participating recipe must use the same `shared` key and archive contents.
The key has 1–128 letters, digits, `.`, `_`, `+`, or `-`, starting with a letter
or digit. `SOURCE_DIR` becomes
`$ROOT_BUILD_DIR/shared-source/<key>/<archive-sha256>/source`; normal installs
keep this inside `ttrek-venv/build`, and standalone scripts inside their `build`
directory. Build and log directories remain private to each package. Ordinary
package source cleanup never removes shared trees.

Preparation is locked, extracts into a temporary directory, and publishes a
completion marker and read-only source tree together. Failed or interrupted
extractions are retried; changed archive contents get a new entry. Recipe
downloads are published atomically so concurrent packages cannot read a partial
download. Shared preparation requires `sha256sum` or `shasum`, and `flock` or
Perl (the fallback on macOS), in both install modes.

A shared recipe must have exactly one enabled unpack, with no `destination`,
`patch`, `git`, `autogen`, or `SOURCE_DIR` override. Other build commands must
respect the read-only source tree and write generated files into `BUILD_DIR`.
Permissions catch accidental writes; this is not a sandbox for untrusted recipes.
Keep recipes that add dependencies or generate files inside the sources private
(AWS core, for example, unpacks its CRT dependencies there). Old hash entries
remain available to other builds; remove the shared cache only when no builds
are using it. Read-only directories need write permission restored before removal.

`cmake_config.source` selects another CMake project directory; it defaults to
`$SOURCE_DIR` and supports build-directory variables, such as
`"source": "$DOWNLOAD_DIR/service-build"`. This lets a standalone build project
use unmodified upstream sources and installed dependencies.

After upstream download attempts fail, native and generated-script builds try
`/download/<package>-<version>-<sha256-of-source-url>.archive` on ttrek.sh.
An explicit `filename` is URL-encoded in the query string for server logging.
It does not affect the mirror key. The endpoint redirects to a signed S3 URL.
The URL digest identifies the source; it does not verify the downloaded bytes.
Direct `ttrek download URL FILE ?RECIPE-VERSION ?FILENAME??` calls use the mirror
only when a recipe identifier is supplied.

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
