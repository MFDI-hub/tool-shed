# repo_map

Budgeted symbol index so a model can **choose** files. This is not a paste
([`skeleton_dump`](skeleton_dump.md) outlines one file; [`sync_chosen_files`](sync_chosen_files.md)
dumps contents). Closest analog is [Aider’s repo map](https://aider.chat/docs/repomap.html);
this tool uses import/signature heuristics (no Tree-sitter, no PageRank).

```text
repo_map [paths ...] [options]
```

Same walker as the dump tool (gitignore, `--ext`, `--include` / `--exclude`).
With no paths or `--dir`, the root is `.`. Default output is stdout.

Files are ranked by how many walked files import them, then by path.
`--seed PATH` pulls that file and its neighbors to the front.

```text
# repo map
# files: 2  tokens: ~40

b.py:
  def hub
  class Core
a.py:
  def leaf
```

Token estimate is `(bytes+3)/4`, same as the dump header. Secret filenames are
**not** skipped. Non-text files are skipped.

## Flags

Values can be `--flag VALUE` or `--flag=VALUE`. `--include` / `--exclude` /
`--include-gitignored` / `--ext` also accept comma-separated lists.

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
| `--seed PATH` | none | Repeatable. Boost this file and its import neighbors. |
| `--max-tokens N` | `2048` | Stop adding files around this many tokens. `0` prints the header only. |
| `--max-files N` | unlimited | Cap how many files appear in the map. |
| `--out PATH` | `-` (stdout) | Write a file. `-` is stdout. |
| `-h`, `--help` | | Print usage. |

## Examples

Stdout, default cwd:

```shell
uv run repo_map --ext py
```

Python under `src`, write a file:

```shell
uv run repo_map --dir src --ext py --out outputs/builds/map.txt
```

Boost the file you are editing:

```shell
uv run repo_map --dir src --ext py --seed src/tools_shed/launch.py
```

Tiny budget:

```shell
uv run repo_map --dir src --ext py --max-tokens 256 --max-files 20
```
