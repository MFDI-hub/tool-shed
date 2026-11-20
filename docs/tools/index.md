# Tools

Each command is a C binary launched as a Python console script. They share the
walker in `native/src/discover.c` (gitignore, include/exclude, hidden files).
New binaries exist only when the job is not already a flag on
[`sync_chosen_files`](sync_chosen_files.md).

| Tool | Job |
|---|---|
| [`sync_chosen_files`](sync_chosen_files.md) | Dump/sync chosen text files |
| [`watch_run`](watch_run.md) | Run a command when files change |
| [`skeleton_dump`](skeleton_dump.md) | Outline dump: signatures, drop bodies |
| [`hash_tree`](hash_tree.md) | One content hash of the walked tree |
| [`repo_map`](repo_map.md) | Budgeted symbol index (navigation, not a paste) |
| [`related`](related.md) | Import cone; `--from-git` is impact |
| [`quiet_exec`](quiet_exec.md) | Run a command, print shrunk stdout for an agent |
| [`slice`](slice.md) | One symbol body or line range |
| [`churn`](churn.md) | Git hotspots (path + commit count) |
| [`diff_syms`](diff_syms.md) | Symbols gained/lost vs a git rev |

```shell
uv run python scripts/build_native.py
uv run sync_chosen_files --once
uv run watch_run --dir src -- pytest -q
uv run repo_map --ext py
uv run related --file src/uv_scripts/launch.py --ext py
uv run quiet_exec -- pytest -q
uv run slice src/uv_scripts/launch.py --name find_binary
uv run churn --dir src --ext py
uv run diff_syms --dir src --ext py
```
