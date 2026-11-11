# Registries

[Documentation](readme.md) · [Project overview](../readme.md)

A registry contains package versions, dependencies and build recipes.
Use the public registry by default, or select your own sources below.

## Default endpoints and local development

Registry requests default to `https://ttrek.sh` when no local registry is selected.
With `ENV=LOCAL`, registry, telemetry, fallback-download and release metadata
requests use `http://localhost:8080` for the local ttrek.sh server. Production
release downloads require HTTPS; LOCAL mode permits loopback HTTP for metadata.

## Use a local registry

Use a local registry directory instead of the registry API:

```sh
ttrek gen-script -registry ../ttrek.sh/registry > build.generated.sh
ttrek -registry ../ttrek.sh/registry install -y
ttrek init -registry ../ttrek.sh/registry
```

`-registry` also works with `pretend` and `update`. The directory contains
`package/version/ttrek.json` and optional `.diff` patches. Local mode sends no
registry or telemetry requests; source downloads still follow recipe URLs.
`init -registry` saves `registry` in `ttrek-lock.json` for later commands.
An explicit option overrides it without changing the saved value. Relative saved
paths resolve from the project directory.

## Ordered registries

Repeat `-registry` to supply an ordered list of directories and HTTP(S) API URLs:

```sh
ttrek install -y -registry ../private-recipes -registry ../shared-recipes
ttrek -registry https://packages.example.com/registry -add-pubreg install -y
ttrek init -registry ../private-recipes -add-pubreg
```

`-registry` options before and after the command are combined in encounter order.
An explicit list replaces the saved list and implicit defaults. `-add-pubreg`
appends the public default after those sources, regardless of where the flag
appears. It uses `~/.local/ttrek-registry` after a successful `ttrek sync`, otherwise
`https://ttrek.sh/registry`. Repeating the flag adds the public default only once.
With no `-registry` options, the flag appends to the saved project sources.
You can still name the public directory or API explicitly to choose its position.

The first source containing a package owns its versions and build recipes for
that invocation. A version constraint that cannot be met there is an error;
versions are not combined across sources. An absent directory entry or HTTP 404
for package metadata advances to the next source. Unreadable or malformed
metadata, HTTP errors, and missing recipes in the owning source stop the command.
When all sources lack a package, the error names the package and sources searched.

`init` saves multiple sources, HTTP sources and the public default in
`ttrek-lock.json`. For example, `init -registry ../private-recipes -add-pubreg` saves:

```json
{
  "registries": [
    {"directory": "../private-recipes"},
    {"public": true}
  ]
}
```

The `{"public": true}` entry must be last. It resolves the public default on each
invocation without embedding a home directory in the lock file. HTTP sources use
`{"url": "https://packages.example.com/registry"}` entries.

The existing single-directory `registry` setting remains supported. Use either
`registry` or `registries`, not both. Saved relative directories resolve from the
project root; explicit options on other commands do not rewrite these settings.

HTTP sources expose the existing API: `GET BASE/PACKAGE` returns an object mapping
versions to dependencies, and `GET BASE/PACKAGE/VERSION/OS/ARCH` returns the install
recipe. Authentication is not implemented. URLs cannot contain credentials,
queries or fragments. Redirects require configuring the final API URL. Explicit
registry lists send no telemetry. Source archive downloads continue to use recipe
URLs. Search supports ordered local and HTTP sources. HTTP search uses
`GET BASE/search?q=QUERY`, returning an array of objects containing `name`,
`description` and `match` (exact, prefix, substring, description, fuzzy or
description-fuzzy), with optional numeric `distance` and `coverage` fields.
Without a local public registry, search uses the ttrek.sh endpoint automatically.
`clone` uses the same registry selection to fetch a package's source archive.

## Sync the local registry

```sh
ttrek sync                         # ~/.local/ttrek-registry
ttrek sync ./registry              # explicit destination
ttrek sync .                       # current directory must be empty or the registry checkout
```

`sync` requires Git and clones `https://github.com/jerily/ttrek-registry.git`.
Later runs fast-forward a clean `main` checkout of that repository. Local edits,
another repository, another branch, and divergent history are rejected without
overwriting the checkout. Network errors from an explicit sync are reported.

Registry commands select the explicit `-registry` option first, then the saved
project registry, then `~/.local/ttrek-registry` after a successful `ttrek sync`,
then the website. Normal commands never create or clone the default directory;
an absent or unmarked directory leaves website access enabled. With a single
selected local registry, missing packages do not fall back to the website.
A missing package produces one error naming the package and the
registry searched, with a suggestion to check the name or select another registry.
Automatic default selection is not saved in project lockfiles, so projects stay
portable and fall back to the website if that checkout is later removed.

Sync records `ttrek-last-sync` in the checkout's Git metadata directory, including
linked worktrees. Commands warn on stderr if the last sync was over 24 hours ago
or HEAD is behind the fetched `origin/main`. The message suggests `ttrek sync`
for the default checkout or `ttrek sync .` inside a custom checkout. This uses
local metadata and does not claim to know GitHub's current commit. Stale data
remains usable offline. `gen-script` continues to emit only the script on stdout.
