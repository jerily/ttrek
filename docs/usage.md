# Use ttrek

[Documentation](readme.md) · [Project overview](../readme.md)

Start with the [installation guide](install.md) if you do not have ttrek yet.
A package is a reusable library or tool; its dependencies are the other packages
it needs to work. A registry is the catalog ttrek uses to find them.

## Create a project

```sh
mkdir my-app
cd my-app
ttrek init -y
ttrek install -y tcl tjson
```

`ttrek.json` records the packages your project asks for. `ttrek-lock.json`
records the selected versions. Installed packages live in `ttrek-venv/local`
inside the project.

To open Tcl with those packages available:

```sh
ttrek run tclsh9.1
```

At the Tcl prompt, try `package require tjson`. To install an existing project's
declared packages, run `ttrek install -y` from its directory.

## Clone package sources

```sh
ttrek clone tsession
ttrek clone tsession@1.1.0 -outdir ./my-session
```

`clone` downloads and unpacks the newest matching source archive into `./PACKAGE`
or the directory specified by `-outdir`. The destination must be new or empty.
It works outside projects and supports the usual version constraints and registry
options. Dependencies, patches, hooks and build commands are not run. Recipes
without a source archive report an error. See `ttrek help clone` for details.

## Install, preview and update

From the same project directory, preview changes before installing them:

```sh
ttrek pretend tjson twebserver
ttrek install tjson twebserver
ttrek update
```

`pretend` reads the project's `ttrek.json`, optional `ttrek-lock.json` and installed manifest, resolves
against the live registry, and prints the same installation plan as `install`.
It does not create a venv, modify project files, run hooks, install packages,
or initialize/send telemetry. It supports `-force` and
`-strategy latest|favored|locked`; use `-strategy latest` to preview update behavior.
With no package arguments it resolves existing project requirements.
Resolution conflicts and malformed/failed metadata requests return a nonzero exit.

### Development dependencies

`install -save-dev PACKAGE` (or `-D`) records
explicitly named packages in `devDependencies`. Existing runtime dependencies
move to that section even if no rebuild is needed. Plain install and update
resolve both sections in the root project and preserve their classification.
Like npm, installed packages contribute only their `dependencies`; their
`devDependencies` are ignored, even when the package is a root devDependency.
Transitive packages stay in the lockfile and share the project's installation
prefix. A development tool should declare the packages needed by its commands
as dependencies, while the consuming project declares the tool as a devDependency.
Uninstall removes direct entries from either section. A package name must occur
in only one root-project section; duplicate names across those sections are rejected.

### Rebuilds and version selection

`install -force PACKAGE` rebuilds that package and its transitive reverse
dependencies. Changed versions and USE flags also rebuild affected dependents,
in dependency order, while unchanged forward dependencies remain installed.
Targeted updates prefer locked versions outside the request when compatible;
`update` without package arguments considers the entire dependency graph.
See `ttrek help install` for all install options and strategies.

### Lockfiles and installed state

The lockfile records resolution choices. Installed state comes from
`ttrek-venv/ttrek-manifest.json`: each package records its version, files, `iuse`,
`use`, `requires`, and scripts. A lockfile copied into a fresh checkout does not
skip installation. Missing files or mismatching installed versions/flags trigger
reinstallation; old manifests without build metadata are rebuilt once.
Resolution defaults stay `favored` for install and `latest` for update. Update
selects newer versions within the saved constraints and refreshes the lockfile.

Rollback history lives in `ttrek-venv/.ttrek-git`, accessed explicitly by ttrek.
Existing internal `.git` directories are renamed on the next install/update,
preserving history. No `.git` pointer remains for ordinary Git/editor discovery.
An editor that already registered the old repository may need a reload.

Environment initialization, saving and recovery print short status messages.
Set `TTREK_DEBUG` to see internal commit IDs, reset details and deleted paths.

For registries managed by `ttrek sync`, commands warn when the checkout is behind
its fetched `origin/main` or was last synced over 24 hours ago. The warning
suggests `ttrek sync` for the default registry, or `ttrek sync .` from a custom
registry directory. This uses local metadata; it does not fetch during resolution.

## Search recipes

Search uses the synced public registry when available and the ttrek.sh search
endpoint otherwise. Run `ttrek sync` to enable offline search:

```sh
ttrek search tcl
ttrek search web
ttrek search 'web server'
ttrek search -registry /path/to/ttrek-registry tcll
```

Prints `name<TAB>description`. Ranking is exact name, name prefix, name substring,
literal description, name typo, then description typo. Ties prefer fewer edits,
greater query-trigram coverage, less unmatched name text, then alphabetical name.
`web` finds `twebserver`; short queries allow one edit, longer queries two.
Matching ignores ASCII case. Queries accept 1–255 ASCII letters, digits, spaces,
`.`, `_`, `+` or `-`, excluding empty queries, `.` and `..`.

Search uses `-registry`, the nearest project's saved registry, or an existing
existing `~/.local/ttrek-registry`, then the website, in that order. It works outside a project and indexes
names and descriptions from every `version/ttrek.json`, once per package in the
results. Missing descriptions are allowed; description matches display the
matching text, otherwise the first nonempty description in alphabetical order.
Both local indexes are built only when `search` runs. HTTP registries supply
matches through `GET BASE/search?q=QUERY`. Results are ranked together, with
earlier registries taking precedence for duplicate packages. No telemetry,
update checks or project writes occur.
Description suggestions require a shared trigram and pass a Levenshtein substring
check; queries below three characters match descriptions literally.

## More commands

Run `ttrek` or `ttrek help` for the command list, or `ttrek help install` for a command's
options. See [registries](registries.md) to choose where packages come from,
and [scripts](scripts.md) to run package commands or generate standalone builds.
