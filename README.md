# tool-shed

A hub of command-line tools written in C and installed as Python console scripts.
Zig cross-compiles each tool for every OS/CPU that can install a platform wheel
(Windows, macOS, manylinux, musllinux, FreeBSD). A thin Python launcher runs the
binary that matches this machine. The launcher supports Python 3.6 and newer.
Install from PyPI as `tool-shed`.

| Tool | Job |
|---|---|
| [`sync_chosen_files`](docs/tools/sync_chosen_files.md) | Dump/sync chosen text files (gitignore, watch, clipboard, `--list`) |
| [`watch_run`](docs/tools/watch_run.md) | Run a command when files change (`pytest`, `make`, …) |
| [`skeleton_dump`](docs/tools/skeleton_dump.md) | Outline dump: signatures, docstrings, fields; bodies dropped |
| [`hash_tree`](docs/tools/hash_tree.md) | One content hash of the walked tree |
| [`repo_map`](docs/tools/repo_map.md) | Budgeted symbol index so a model can pick files |
| [`related`](docs/tools/related.md) | Import cone; `--from-git` is impact |
| [`quiet_exec`](docs/tools/quiet_exec.md) | Run a command, print shrunk stdout for an agent |
| [`slice`](docs/tools/slice.md) | One symbol body or line range |
| [`churn`](docs/tools/churn.md) | Git hotspots (path + commit count) |
| [`diff_syms`](docs/tools/diff_syms.md) | Symbols gained/lost vs a git rev |

`sync_chosen_files` watches any paths you pick and writes them to a text dump,
honoring gitignore the way git does. Default is every text file; pass `--ext`,
`--include`, and `--exclude` to narrow it. `--once` dumps; omit it to watch.
`--out -` prints to stdout. Secret filenames (`.env`, `*.pem`, `credentials.json`)
are skipped unless you pass `--allow-secrets`.

The other tools exist only for jobs the dump tool does not already do: run a
command on change, emit an outline, hash the tree, index symbols, follow
imports, shrink command output, slice a symbol, or list git hotspots.
`quiet_exec` and `slice` do not use the walker.

```shell
uv tool install tool-shed
sync_chosen_files --once
watch_run --dir src -- pytest -q
```

From a checkout:

```shell
uv sync
uv run python scripts/build_native.py   # needs Zig on PATH
uv run sync_chosen_files --once
```

Each GitHub Release attaches one binary per target plus a `py3-none-<platform>`
wheel that bundles that platform's binaries. The same wheels go to TestPyPI,
then PyPI. macOS binaries produced by Zig are unsigned.

## Development

```shell
uv sync
uv run python scripts/build_native.py
uv run pre-commit install && uv run pre-commit install --hook-type commit-msg
uv run pytest -n auto --cov
```

Install Zig from [ziglang.org/download](https://ziglang.org/download/) (this repo
pins 0.17.1). Commit `uv.lock`. CI runs `uv sync --locked` and fails when the
lockfile is missing or stale.

*This project was built from [python-template](https://github.com/MFDI-hub/python-template).*
