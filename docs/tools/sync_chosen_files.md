# sync_chosen_files

Watch any paths you pick (any language, any text file) and write them to one
text dump. This is the dump/sync tool. Listing, diffs, trees, token totals,
chunking, clipboard, and secret-filename skip are flags here — not separate
CLIs.

```text
sync_chosen_files [paths ...] [options]
```

Directory scans follow gitignore the way git does: `core.excludesFile`
(including `~/.config/git/ignore`), `$GIT_DIR/info/exclude`, then nested
`.gitignore` files, last match wins. A parent directory that is ignored still
hides its children, even if a later `!` pattern names a file inside. Pass a
path explicitly to dump it anyway — a file is always included; a directory is
still walked, while nested ignore rules inside it continue to apply.

`.git` and non-text files are always skipped. Default is every text file.
Default output is `outputs/builds/source_dump.txt` (gitignored). Omit `--once`
to watch and rewrite on change.

If you pass no paths, `--dir`, or `--file-list`, it reads
`scripts/chosen_files.txt` (one path per line).

## Flags

### Inputs

| Flag | Default | What it does |
|---|---|---|
| `paths …` | (none) | Files or directories to dump. A file is included even if gitignored. |
| `--dir PATH` | | Extra root to scan. Repeatable. Same as a positional directory. |
| `--file-list PATH` | `scripts/chosen_files.txt` if nothing else is given | Text file of paths, one per line. `#` comments allowed. |
| `--` | | Everything after `--` is a path, even if it looks like a flag. |

### Outputs

| Flag | Default | What it does |
|---|---|---|
| `--out PATH` | `outputs/builds/source_dump.txt` | Destination. Repeatable. `-` is stdout. `clipboard` / `clip` copies. |
| `--also` | | Start another output that inherits the current filters, then set its own `--out` / `--ext` / … |
| `--mode MODE` | `code` | `code`, `concat`, `lines`, `markdown` (`md`), `cxml` (`xml`), `json`. |
| `--copy` | off | Also copy the dump to the clipboard. |
| `--append` | off | Append instead of replacing the file. |
| `--no-header` | header on | Skip the `# synced dump` stats block. |
| `--tree` | on | Include a path tree in the header. |
| `--no-tree` | | Omit the tree. |
| `--line-numbers` | off | Prefix each dumped line with a number. |
| `--no-dedupe` | dedupe on | Lines mode: keep duplicate lines. |
| `--sort-longest` | off | Lines mode: sort lines longest-first. |

**Modes**

- `code` — `FILE: path` banners and file contents (default).
- `concat` — raw concatenation, no banners.
- `lines` — one unique line per line of input (unless `--no-dedupe`).
- `markdown` — fenced code blocks per file.
- `cxml` — Claude-style XML `<document>` wrappers.
- `json` — JSON object of paths to contents.

### Selection

| Flag | Default | What it does |
|---|---|---|
| `--ext EXTS` | all text (`*`) | Suffix filter, comma-separated. Example: `py,ts,md`. `*` or `all` means every text file. |
| `--include GLOB` | (none) | Keep only relative paths matching at least one glob. Repeatable / CSV. |
| `--exclude GLOB` | (none) | Skip matching relative paths. Repeatable / CSV. |
| `--include-gitignored GLOB` | (none) | Keep gitignored files that match. |
| `--ignore-file PATH` | | Extra gitignore-style rules. Repeatable. `.syncignore` in cwd and each root is always loaded. |
| `--hidden` | on | Include dotfiles / hidden path components. |
| `--no-hidden` | | Skip hidden path components. |
| `--follow-symlinks` | off | Follow directory symlinks while walking. |
| `--no-gitignore` | gitignore on | Do not apply gitignore. |
| `--allow-secrets` | off | Include `.env`, `.env.*`, `credentials.json`, `secrets.json`, `.netrc`, `id_rsa` / `id_ed25519` / …, `*.pem` / `*.key` / `*.p12` / `*.pfx` / `*.p8`. |
| `--diff` | off | Only files newer than the output file. |
| `--diff-git [REV]` | | Only files changed vs git `REV` (default `HEAD`), plus untracked when `REV` is `HEAD`. |
| `--newer-than WHEN` | | Keep files with mtime ≥ `WHEN`. |
| `--older-than WHEN` | | Keep files with mtime &lt; `WHEN`. |

`WHEN` is an ISO date (`2024-01-01` or `2024-01-01T12:00:00`), unix seconds, or a git revision.

### Size

| Flag | Default | What it does |
|---|---|---|
| `--max-files N` | unlimited | Stop after N files. |
| `--max-bytes N` | unlimited | Stop adding files once the dump would exceed N bytes. |
| `--max-file-bytes N` | unlimited | Skip a file larger than N (unless `--truncate` or the path was passed explicitly). |
| `--truncate` | off | Truncate oversize files instead of skipping them. |
| `--chunk-bytes N` | off | Split into `out-1`, `out-2`, … at file boundaries. |

`N` accepts `k` / `m` / `g` suffixes (`80k`, `2m`).

### Watch and plan

| Flag | Default | What it does |
|---|---|---|
| `--once` | off (watch) | Write once and exit. |
| `--interval SECONDS` | `2` | Poll / native-watch timeout. |
| `--debounce SECONDS` | `0.25` | Quiet period before a rewrite. |
| `--poll` | native watch | Force polling. |
| `--list` | | Print included paths (`+ rel`) and exit. Implies `--once`. |
| `--dry-run` | | Print included and skipped paths, do not write. Implies `--once`. |
| `--config PATH` | auto `.sync_chosen_files` | Extra args from a file. |
| `--no-config` | | Skip the auto config file. |
| `-h`, `--help` | | Print usage. |

A `.sync_chosen_files` file in the working directory is loaded as extra args
unless `--no-config`. Each line is a flag or `key = value` (`dir = src`).

## Examples

One-shot dump of the default file list:

```shell
uv run sync_chosen_files --once
```

Watch and rewrite on change:

```shell
uv run sync_chosen_files
```

Positional paths, stdout, markdown:

```shell
uv run sync_chosen_files src tests --once --out - --mode markdown
```

Directory via `--dir`, only some suffixes:

```shell
uv run sync_chosen_files --dir src --ext py,ts,md --once
```

File list:

```shell
uv run sync_chosen_files --file-list scripts/chosen_files.txt --once
```

Dump a gitignored folder by passing it explicitly:

```shell
uv run sync_chosen_files --dir ignored_pkg --once
```

Include / exclude globs:

```shell
uv run sync_chosen_files --dir . --include 'src/**' --exclude '*_test.py' --once
```

Keep a gitignored pattern:

```shell
uv run sync_chosen_files --dir . --include-gitignored 'vendor/**' --once
```

Extra ignore file, no gitignore, include hidden, follow symlinks:

```shell
uv run sync_chosen_files --dir . --ignore-file extra.ignore --no-gitignore --hidden --follow-symlinks --once
```

Skip hidden files:

```shell
uv run sync_chosen_files --dir . --no-hidden --once
```

Include secret filenames:

```shell
uv run sync_chosen_files --dir . --once --allow-secrets
```

Only files newer than the dump, or newer/older than a date or git rev:

```shell
uv run sync_chosen_files --dir . --out dump.txt --once --diff
uv run sync_chosen_files --dir . --once --newer-than 2024-01-01
uv run sync_chosen_files --dir . --once --older-than HEAD
uv run sync_chosen_files --dir . --once --diff-git
uv run sync_chosen_files --dir . --once --diff-git origin/main
```

Two outputs with `--also`:

```shell
uv run sync_chosen_files --dir . --out all.txt --also --out py.txt --ext py --once
```

Modes, clipboard, append, header/tree/line numbers:

```shell
uv run sync_chosen_files --dir . --once --mode concat --no-header --no-tree
uv run sync_chosen_files --dir . --once --mode lines --sort-longest --no-dedupe
uv run sync_chosen_files --dir . --once --mode json --out -
uv run sync_chosen_files --dir . --once --mode cxml --line-numbers
uv run sync_chosen_files --dir . --once --copy
uv run sync_chosen_files --dir . --once --out clipboard
uv run sync_chosen_files --dir . --once --out dump.txt --append
```

Size caps and chunking:

```shell
uv run sync_chosen_files --dir . --once --max-files 50 --max-bytes 200k
uv run sync_chosen_files --dir . --once --max-file-bytes 32k
uv run sync_chosen_files --dir . --once --max-file-bytes 32k --truncate
uv run sync_chosen_files --dir . --once --out dump.txt --chunk-bytes 80k
```

Plan only:

```shell
uv run sync_chosen_files --dir . --list
uv run sync_chosen_files --dir . --dry-run
```

Watch tuning:

```shell
uv run sync_chosen_files --dir src --interval 1 --debounce 0.5
uv run sync_chosen_files --dir src --poll --interval 3
```

Config file:

```shell
uv run sync_chosen_files --config my.sync
uv run sync_chosen_files --no-config --dir . --once
```

Paths that look like flags:

```shell
uv run sync_chosen_files --once -- --dir-named-like-a-flag
```
