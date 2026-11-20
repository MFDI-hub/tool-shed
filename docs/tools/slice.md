# slice

One symbol body or line range from a file. Not a dump
([`sync_chosen_files`](sync_chosen_files.md)) and not an outline of every
signature ([`skeleton_dump`](skeleton_dump.md) / [`repo_map`](repo_map.md)).

```text
slice FILE [--name NAME] [--lines A-B[,C-D]] [--out PATH]
```

`FILE` is required and is read even if gitignored. Need `--name` or `--lines`
(exit 2 otherwise). Unknown `--name`: stderr and exit 1.

`--name` finds the first signature line whose text contains that name as a
word (`def foo`, `class Foo`, `function foo`). Python/Ruby keep lines until a
later non-blank line with indent `<=` the signature. Other languages take the
matching `{ … }`.

`--lines` is 1-based inclusive, comma-separated (`10-20,40-42`).

```text
# slice
# file: mod.py

def foo():
    return 1
```

## Flags

| Flag | Default | What it does |
|---|---|---|
| `FILE` | required | Source file. |
| `--name NAME` | | Repeatable. Emit that symbol’s body. |
| `--lines RANGES` | | Repeatable / CSV. Inclusive line ranges. |
| `--out PATH` | `-` (stdout) | Write a file. `-` is stdout. |
| `-h`, `--help` | | Print usage. |

## Examples

One function:

```shell
uv run slice src/tool_shed/launch.py --name find_binary
```

Line range:

```shell
uv run slice src/tool_shed/launch.py --lines 80-110
```

Several names:

```shell
uv run slice native/src/fs.c --name fs_read_all --name fs_write_all
```

Write a file:

```shell
uv run slice src/mod.py --name foo --out outputs/builds/foo.txt
```
