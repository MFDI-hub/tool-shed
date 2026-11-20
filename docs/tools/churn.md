# churn

Git hotspots: how often walked files show up in recent commits. Paths and
counts only — not a paste, not dump `--diff-git`, and not
[`related --from-git`](related.md) (that is the *current* diff cone).

```text
churn [paths ...] [options]
```

Same walker as the dump tool. `git log --since=<days>.days --name-only` counts
slash-normalized paths that are also in the walked set. Sorted by count
descending, then path. If git fails, exit 1.

```text
# churn
# days: 90  files: 2

12  a.py
3   b.py
```

## Flags

| Flag | Default | What it does |
|---|---|---|
| `paths …` | `.` if none | Files or directories to scan. |
| `--dir PATH` | | Extra root. Repeatable. |
| `--ext EXTS` | all text | Suffix filter. Example: `py,c,ts`. |
| `--include GLOB` | (none) | Keep only matching relative paths. |
| `--exclude GLOB` | (none) | Skip matching relative paths. |
| `--include-gitignored GLOB` | (none) | Keep gitignored files that match. |
| `--ignore-file PATH` | | Extra gitignore-style rules. Repeatable. |
| `--hidden` | on | Include hidden path components. |
| `--no-hidden` | | Skip hidden path components. |
| `--follow-symlinks` | off | Follow directory symlinks. |
| `--no-gitignore` | gitignore on | Do not apply gitignore. |
| `--days N` | `90` | How far back `git log` looks. |
| `--top N` | `20` | How many paths to print. `0` = all. |
| `--out PATH` | `-` (stdout) | Write a file. `-` is stdout. |
| `-h`, `--help` | | Print usage. |

## Examples

Hot Python files, last 90 days:

```shell
uv run churn --dir src --ext py
```

Shorter window, more rows:

```shell
uv run churn --dir . --ext py,c --days 30 --top 50
```

Write a file:

```shell
uv run churn --dir src --ext py --out outputs/builds/churn.txt
```
