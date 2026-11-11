# Install ttrek

[Documentation](readme.md) · [Project overview](../readme.md)

## Install the command-line tool

On Linux or macOS, run:

```sh
curl --proto '=https' --tlsv1.2 -sSf https://ttrek.sh/init | sh
```

Open a new terminal after installation so your shell picks up the executable.
Check that it is available with `ttrek version`, which also reports the
self-update platform and telemetry/debug status without network access.
To compile ttrek yourself, see [Build from source](build.md).

## Install build tools

Packages are built from source, so installing ttrek is only the first step.
Install the tools below before adding packages to a project.

`ttrek install` and `ttrek update` check for Bash, CMake, Make, curl, Git,
patch, pkg-config, Perl, tar, gzip and C/C++ compilers before creating project
state, contacting a registry or running hooks. Missing tools are reported
together with installation commands. `CC` and `CXX` select explicit compiler
commands; otherwise GCC or Clang are accepted. Read-only commands such as
`search`, `pretend` and `gen-script` do not require a build toolchain.

On Ubuntu/Debian:

```sh
sudo apt update
sudo apt install build-essential cmake bash curl git patch pkg-config perl tar gzip ca-certificates
```

[build-essential](https://packages.ubuntu.com/noble/build-essential) supplies
GCC, G++ and Make. For a newer CMake, use
`sudo snap install cmake --classic` and include `/snap/bin` on `PATH`
([CMake distribution](https://cmake.org/download/)).

On macOS, install Apple's Command Line Tools and wait for installation to finish:

```sh
xcode-select --install
```

The [Command Line Tools](https://developer.apple.com/documentation/xcode/installing-the-command-line-tools/)
provide Clang for C/C++, Make and Git. ttrek accepts Clang; a separate GCC
installation is unnecessary. macOS supplies Bash, curl, patch, Perl, tar and gzip.
Install [Homebrew](https://brew.sh/) if needed and follow its printed `brew shellenv`
instructions so its tools are on `PATH`. Then install the remaining tools:

```sh
brew install cmake pkgconf
```

[pkgconf](https://formulae.brew.sh/formula/pkgconf) provides the `pkg-config`
command. Building ttrek itself and running its tests additionally needs Python 3
(`brew install python`). These are presence checks; individual recipes may
require additional tools or minimum tool versions.

## Update the ttrek executable

```sh
ttrek version                      # version, platform and telemetry/debug status
ttrek self-update -version         # version number only; no network access
ttrek self-update -check           # check without installing
ttrek self-update                  # download, verify and replace this executable
```

`update` continues to update project packages. `self-update` reads
`https://ttrek.sh/releases/latest/<target>`, selects the running executable's
architecture and libc, and uses the signed download URL from the metadata.
It verifies SHA-256 and the downloaded program's reported version before an
atomic replacement in the installation directory. Errors leave the existing
binary intact, and temporary files are removed. Symlinks remain intact; their
resolved target is replaced. The directory must be writable, and no sudo is run.
macOS universal2 installations update to the currently executing architecture.

Successful interactive `init`, `install`, `update`, `uninstall`, `ls`, `run`, and
`sync` commands also check at most once per day, with a one-second request
timeout. Connection and metadata failures are ignored; notices go to stderr.
Noninteractive commands, `pretend`, and `gen-script` do not check. Set
`TTREK_NO_UPDATE_CHECK=1` to disable automatic checks. Check timestamps live under
`~/.local/state/ttrek/`; this does not create the local registry directory.

## Next steps

Follow the [usage guide](usage.md) to create a project and install packages,
or try [OpenACS](openacs/readme.md) or the [Tk widget demos](tk/readme.md).
