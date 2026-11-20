# hash_tree

One SHA-256 of the walked tree. Change detection, not a paste.

```text
hash_tree [paths ...] [options]
```

Files are discovered with the same walker as
[`sync_chosen_files`](sync_chosen_files.md). Each file contributes
`relative/path<NUL>contents<NUL>` in relative-path order, with `\` normalized
to `/` so Windows and Unix agree. Prints 64 hex digits and a newline.

With no paths or `--dir`, the root is `.`. Default output is stdout. Empty tree
hashes the empty input (`e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`).

Binary files are included (this is a content hash, not a paste). Secret
filenames are **not** skipped here.

## Flags

| Flag | Default | What it does |
|---|---|---|
| `paths …` | `.` if none | Files or directories to hash. A file given explicitly is included even if gitignored. |
| `--dir PATH` | | Extra root. Repeatable. |
| `--ext EXTS` | all files | Suffix filter, comma-separated. Example: `py,ts,md`. `*` / `all` = every file. |
| `--include GLOB` | (none) | Keep only relative paths matching at least one glob. Repeatable / CSV. |
| `--exclude GLOB` | (none) | Skip matching relative paths. Repeatable / CSV. |
| `--include-gitignored GLOB` | (none) | Keep gitignored files that match. |
| `--ignore-file PATH` | | Extra gitignore-style rules. Repeatable. `.syncignore` is also loaded. |
| `--hidden` | on | Include hidden path components. |
| `--no-hidden` | | Skip hidden path components. |
| `--follow-symlinks` | off | Follow directory symlinks. |
| `--no-gitignore` | gitignore on | Do not apply gitignore. |
| `--out PATH` | `-` (stdout) | Write the hash to a file instead of stdout. |
| `-h`, `--help` | | Print usage. |

## Examples

Hash `src`, Python only, stdout:

```shell
uv run hash_tree --dir src --ext py
```

Positional roots, write a file:

```shell
uv run hash_tree src tests --out outputs/builds/tree.sha256
```

Include / exclude:

```shell
uv run hash_tree --dir . --include 'src/**' --exclude 'tests/**'
```

Gitignored paths that should still affect the hash:

```shell
uv run hash_tree --dir . --include-gitignored 'vendor/**'
```

Extra ignore, no gitignore, hidden, symlinks:

```shell
uv run hash_tree --dir . --ignore-file extra.ignore --no-gitignore --hidden --follow-symlinks
```

Skip hidden files:

```shell
uv run hash_tree --dir . --no-hidden
```

Compare two trees in a script:

```shell
a=$(uv run hash_tree --dir src --ext py)
# … edit …
b=$(uv run hash_tree --dir src --ext py)
[ "$a" = "$b" ] || echo changed
```
