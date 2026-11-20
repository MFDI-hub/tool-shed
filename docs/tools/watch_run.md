# watch_run

Run a command when files change. Selection uses the same walker as
[`sync_chosen_files`](sync_chosen_files.md) (gitignore, include/exclude,
extensions). The dump tool watches to rewrite a dump; this runs `pytest`,
`make`, and so on.

```text
watch_run [paths ...] [options] -- command [args...]
```

The command is required and must come after `--`. With no paths or `--dir`,
the root is `.`. On start it runs the command once (unless `--no-initial` or
`--once`). On change it re-walks the tree and reruns only when the discovered
file set (path, mtime, size) actually changed, so gitignored files do not
trigger a run.

## Flags

| Flag | Default | What it does |
|---|---|---|
| `paths …` | `.` if none | Files or directories to watch. |
| `--dir PATH` | | Extra root. Repeatable. |
| `--ext EXTS` | all files | Suffix filter, comma-separated. Example: `py,ts`. `*` / `all` = every file. |
| `--include GLOB` | (none) | Keep only relative paths matching at least one glob. Repeatable / CSV. |
| `--exclude GLOB` | (none) | Skip matching relative paths. Repeatable / CSV. |
| `--include-gitignored GLOB` | (none) | Keep gitignored files that match (so they can trigger a run). |
| `--ignore-file PATH` | | Extra gitignore-style rules. Repeatable. `.syncignore` is also loaded. |
| `--hidden` | on | Include hidden path components. |
| `--no-hidden` | | Skip hidden path components. |
| `--follow-symlinks` | off | Follow directory symlinks. |
| `--no-gitignore` | gitignore on | Do not apply gitignore. |
| `--interval SECONDS` | `2` | Poll / native-watch timeout. |
| `--debounce SECONDS` | `0.25` | Quiet period before a rerun. |
| `--poll` | native watch | Force polling. |
| `--once` | off | Run the command once and exit (no watch). |
| `--no-initial` | off | Wait for a change before the first run. |
| `--` | required | Everything after this is the command and its args. |
| `-h`, `--help` | | Print usage. |

## Examples

Run tests whenever `src` changes:

```shell
uv run watch_run --dir src -- pytest -q
```

Positional roots, only Python:

```shell
uv run watch_run src tests --ext py -- pytest -q
```

Include / exclude:

```shell
uv run watch_run --dir . --include 'src/**' --exclude '*_test.py' -- make
```

Gitignored files that should still trigger:

```shell
uv run watch_run --dir . --include-gitignored 'generated/**' -- make build
```

Extra ignore, no gitignore, hidden, symlinks:

```shell
uv run watch_run --dir . --ignore-file extra.ignore --no-gitignore --hidden --follow-symlinks -- make
```

Skip hidden files:

```shell
uv run watch_run --dir . --no-hidden -- pytest -q
```

Run once (useful in scripts/tests):

```shell
uv run watch_run --dir src --once -- python -m compileall src
```

Do not run until something changes:

```shell
uv run watch_run --dir src --no-initial -- pytest -q
```

Polling and timing:

```shell
uv run watch_run --dir src --poll --interval 1 --debounce 0.5 -- pytest -q
```

Command with its own flags (the `--` is what separates them):

```shell
uv run watch_run --dir src --ext py -- python -m pytest -q --tb=short
```
