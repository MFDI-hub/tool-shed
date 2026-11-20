# related

Import cone around seed files. One relative path per line. This is not a dump
and not a skeleton — pipe the paths into those tools if you want contents.

`--from-git` **is** impact: git-changed files plus their neighbors. That is
different from dump `--diff-git`, which only *restricts* a paste to changed
paths.

```text
related [paths ...] [options]
```

Same walker as the dump tool. With no `--dir` / directory paths, the root is
`.`. Default output is stdout. Exit `2` if there is no seed (`--file`, a
positional **file**, or `--from-git`).

```text
# related
# seeds: 1
a.py
b.py
```

Edges come from the same heuristics as [`repo_map`](repo_map.md):
`import` / `from` / `#include "..."` / relative JS `from`/`require` /
`require_relative`. Unresolved imports are dropped. Obvious tests by name
(`test_<stem>`, `<stem>_test`, `<stem>.test.*`, `<stem>.spec.*`) in the walked
set are attached.

## Flags

| Flag | Default | What it does |
|---|---|---|
| `paths …` | `.` if none | A **file** is a seed (and a scan root). A **directory** is a scan root only. |
| `--dir PATH` | | Extra scan root. Repeatable. |
| `--ext EXTS` | all text | Suffix filter. Example: `py,c,ts`. |
| `--include GLOB` | (none) | Keep only matching relative paths. |
| `--exclude GLOB` | (none) | Skip matching relative paths. |
| `--include-gitignored GLOB` | (none) | Keep gitignored files that match. |
| `--ignore-file PATH` | | Extra gitignore-style rules. Repeatable. |
| `--hidden` | on | Include hidden path components. |
| `--no-hidden` | | Skip hidden path components. |
| `--follow-symlinks` | off | Follow directory symlinks. |
| `--no-gitignore` | gitignore on | Do not apply gitignore. |
| `--file PATH` | | Seed. Repeatable. Basename or relative path. |
| `--from-git [REV]` | off | Seeds = `git diff --name-only --diff-filter=ACMR REV` (default `HEAD`). When `REV` is `HEAD`, also untracked (`git ls-files --others --exclude-standard`). |
| `--depth N` | `1` | Hops from each seed. |
| `--direction both\|imports\|importers` | `both` | Follow imports, importers, or both. |
| `--out PATH` | `-` (stdout) | Write a file. `-` is stdout. |
| `-h`, `--help` | | Print usage. |

## Examples

Files imported by `a.py` and files that import it:

```shell
uv run related --dir src --ext py --file a.py
```

Imports only:

```shell
uv run related --dir src --ext py --file a.py --direction imports
```

Deeper cone:

```shell
uv run related --dir src --ext py --file a.py --depth 2
```

Impact of the working tree vs `HEAD`:

```shell
uv run related --dir src --ext py --from-git
```

Vs another revision:

```shell
uv run related --dir src --ext py --from-git origin/main
```

Positional file as seed:

```shell
uv run related src/uv_scripts/launch.py --ext py
```
