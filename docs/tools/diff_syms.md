# diff_syms

Symbols gained (`+`), lost (`-`), or still present (`=`) in files changed vs a
git revision. No bodies. Not a dump, not a skeleton, and not
[`related --from-git`](related.md) (that lists **files**).

```text
diff_syms [paths ...] [options]
```

Same walker as the dump tool. Changed paths come from
`git diff --name-only --diff-filter=ACMR REV` (default `HEAD`), plus untracked
when `REV` is `HEAD`. Each path is compared as signatures now vs
`git show REV:path` (missing old file → all current names are `+`). Files with
no signature add/remove are omitted; `=` lines print only when the file also
has a `+` or `-`.

```text
# diff_syms
# rev: HEAD

mod.py:
  = def old
  + def new
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
| `--rev REV` | `HEAD` | Compare against this revision. |
| `--out PATH` | `-` (stdout) | Write a file. `-` is stdout. |
| `-h`, `--help` | | Print usage. |

## Examples

Working tree vs `HEAD`:

```shell
uv run diff_syms --dir src --ext py
```

Vs another branch:

```shell
uv run diff_syms --dir src --ext py --rev origin/main
```

Write a file:

```shell
uv run diff_syms --dir src --ext py --out outputs/builds/syms.txt
```
