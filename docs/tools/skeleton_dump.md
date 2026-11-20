# skeleton_dump

Outline dump for AI context: keep the shape of a tree, drop the bodies. This is
not a `--mode` on [`sync_chosen_files`](sync_chosen_files.md). Closest public
analogs are [Repomix `--compress`](https://repomix.com/guide/code-compress) and
[astpack](https://github.com/OLIX-boop/skeleton-cli); those parse with
Tree-sitter. This tool uses line/brace heuristics (no parser, no extra
runtime).

```text
skeleton_dump [paths ...] [options]
```

Same walker as the dump tool (gitignore, `--ext`, `--include` / `--exclude`).
With no paths or `--dir`, the root is `.`. Always one-shot (there is no
`--once` / watch). Default output is stdout.

Flags that exist on the dump tool on purpose **not** here: `--list`, `--tree`,
`--mode`, `--copy`, `--chunk-bytes`, `--allow-secrets`, `--diff`. Use
[`sync_chosen_files`](sync_chosen_files.md) for a full paste, clipboard, or a
plan.

Every keep category is a `--name` / `--no-name` pair. Passing nothing is
today’s skeleton: imports, signatures, comments, docs, and fields on; function
bodies collapsed; object literals collapsed; `if` / `for` bodies kept.
`--outline` and `--focus` are presets, not extra dump modes. Last flag wins.
`--outline` applies first, then a later `--docs` can turn docs back on.

## What it keeps / drops

**Python** — extensions `.py`, `.pyi`, `.pyw` (or a shebang / `--lang py` on a
file with no known extension):

| Kept (defaults) | Dropped (defaults) |
|---|---|
| `import` / `from` | Function and method bodies (`return`, `await`, nested `def` inside a `def`, …) replaced with `...` |
| `#` comments and `@` decorators | Lowercase module-level assignments (`skip_me = 2`) |
| `class` / `def` / `async def` signatures, including multi-line parameter lists | `async with` / `async for` (not declarations) |
| Module, class, and function docstrings (`"""` / `'''`, including `r`/`f`/`b` prefixes) | Blank lines (collapsed away) |
| Class fields (annotations and defaults, so dataclasses stay readable) | |
| `if TYPE_CHECKING:` and its imports | |
| Module-level `FOO = …`, `__all__`, `name: T`, and `type` aliases | |

**Ruby** — extension `.rb` (or a `ruby` shebang / `--lang rb` on an unknown
extension): same indent walker as Python for `require` / `require_relative`,
`module` / `class` / `def`, and `#` comments.

**Brace languages** — extensions `.c`, `.h`, `.cc`, `.cpp`, `.cxx`, `.hpp`,
`.js`, `.jsx`, `.ts`, `.tsx`, `.go`, `.rs`, `.java`, `.cs`, `.kt`, `.swift`,
`.m`, `.mm` (or `--lang c` on an unknown extension). One `{` heuristic for all
of them:

| `{` after | Default |
|---|---|
| Function-like `)` (`foo()`, `func (t *T) M()`) | Collapse to `{ ... }` unless `--bodies` |
| `if` / `for` / `while` / `switch` / `catch` `)` | Keep the body unless `--collapse-control` |
| `=` / `=>` object (`const cfg = {`) | Collapse unless `--no-collapse-objects` |
| `type` / `interface` / `enum` assigned `{` (`type Foo = { a: number }`) | Keep members even while objects collapse |
| Anything else (`struct Foo {`, `class Foo {`) | Keep |

`--no-fields` drops indented members inside those kept type/interface/enum
bodies. `--no-imports` drops `#include` / `import` / `use` / `package`.
`--no-comments` drops `//` and `/* */`. `--no-docs` drops `/** */`.
`--no-signatures` drops collapsed `foo() { ... }` lines; `FILE:` banners still
print.

**Everything else** (Markdown, TOML, PHP, …): keep indent-0 lines; replace
each indented block with a single `    ...`. `--lang` does **not** force
Python/Ruby/C onto files that already have a known extension.

Non-text files are skipped. Secret filenames are **not** skipped (that filter
is dump-only). Unreadable files are omitted. Files are listed in relative-path
order. `\` in paths is left as the walker produced it.

If every keep flag is off, each file is still listed (`FILE: path`) with an
empty body.

## Output

```text
# skeleton dump
# files: 2

================================================================================
FILE: src/mod.py
================================================================================
import os
def foo():
    ...

================================================================================
FILE: src/lib.c
================================================================================
#include <stdio.h>
int foo(void) { ... }
```

Writing a file also prints `wrote skeleton of N file(s)` to stdout. `--out -`
(the default) writes only the dump, no status line.

`--out PATH` creates parent directories if needed. Exit `0` on success, `1` if
the file could not be written.

## Flags

Values can be `--flag VALUE` or `--flag=VALUE`. `--include` / `--exclude` /
`--include-gitignored` / `--ext` / `--focus` also accept comma-separated lists
and can be repeated.

| Flag | Default | What it does |
|---|---|---|
| `paths …` | `.` if none | Files or directories to scan. A **file** given explicitly is included even if gitignored. A **directory** is walked; nested gitignore still applies. |
| `--dir PATH` | | Extra root. Repeatable. Same as a positional directory. |
| `--ext EXTS` | all text (`*` / `all`) | Suffix filter without the dot. Example: `py,c,ts`. |
| `--include GLOB` | (none) | Keep only relative paths matching at least one glob. Repeatable / CSV. |
| `--exclude GLOB` | (none) | Skip matching relative paths (files and directories). Repeatable / CSV. |
| `--include-gitignored GLOB` | (none) | Keep gitignored files that match. Requires a glob. |
| `--ignore-file PATH` | | Extra gitignore-style rules. Repeatable. `.syncignore` in cwd and each root is also loaded. |
| `--hidden` | on | Include hidden path components. |
| `--no-hidden` | | Skip hidden path components. |
| `--follow-symlinks` | off | Follow directory symlinks while walking. |
| `--no-gitignore` | gitignore on | Do not apply gitignore (`.syncignore` / `--ignore-file` still apply). |
| `--out PATH` | `-` (stdout) | Write a file. `-` is stdout. |
| `--lang {auto,py,rb,c}` | `auto` | Per-file extension. Shebang if there is no extension. `py` / `rb` / `c` apply only to **unknown** extensions — not a global “everything is Python” switch. |
| `--focus GLOB` | none | Repeatable. Matching files ignore the keep flags and emit **full source**. |
| `--imports` / `--no-imports` | on | `import` / `from` / `#include` / `use` / `require` / `package`. |
| `--signatures` / `--no-signatures` | on | `class` / `def` / `function` / `fn` / `func` / `struct` lines (bodies still collapsed unless `--bodies`). |
| `--docs` / `--no-docs` | on | Docstrings / `/** */` on declarations. |
| `--comments` / `--no-comments` | on | `#` / `//` / `/* */`. |
| `--fields` / `--no-fields` | on | Class/dataclass fields and type-literal members. |
| `--bodies` / `--no-bodies` | **off** | Keep function/method bodies instead of `...` / `{ ... }`. Whole-tree `--bodies` is a full paste of those files (use `--focus` when only some files should be full). |
| `--collapse-objects` / `--no-collapse-objects` | collapse on | `const cfg = { ... }` vs keep the object. |
| `--collapse-control` / `--no-collapse-control` | collapse **off** | `if (x) { ... }` vs keep the `if` body. |
| `--outline` | off | Preset: `--no-docs --no-comments --no-fields --no-bodies`. Leaves imports+signatures unless the user also passed `--no-imports` / `--no-signatures`. |
| `-h`, `--help` | | Print usage. |

## Before / after

Python:

```python
"""Module doc."""
from typing import TYPE_CHECKING

FOO = 1
skip_me = 2

if TYPE_CHECKING:
    from collections.abc import Mapping

@dataclass
class Item:
    """An item."""
    x: int
    y: str = "a"
    def meth(self) -> int:
        """Return x."""
        return self.x

async def run(
    n: int,
) -> None:
    await n
```

becomes:

```python
"""Module doc."""
from typing import TYPE_CHECKING
FOO = 1
if TYPE_CHECKING:
    from collections.abc import Mapping
@dataclass
class Item:
    """An item."""
    x: int
    y: str = "a"
    def meth(self) -> int:
        """Return x."""
        ...
async def run(
    n: int,
) -> None:
    ...
```

C:

```c
#include <stdio.h>
int foo(void) {
    return 1;
}
```

becomes:

```c
#include <stdio.h>
int foo(void) { ... }
```

TS — type literals stay; objects collapse:

```ts
type Foo = { a: number }
const cfg = { a: 1 }
```

becomes:

```ts
type Foo = { a: number }
const cfg = { ... }
```

JS:

```js
const cfg = {
    secret: 1,
    nested: { x: 2 }
};
export function f() {
    return cfg;
}
```

becomes:

```js
const cfg = { ... };
export function f() { ... }
```

## Examples

Stdout, default cwd:

```shell
uv run skeleton_dump
```

Python and C under `src`, write a file:

```shell
uv run skeleton_dump --dir src --ext py,c --out outputs/builds/skeleton.txt
```

Equals form and CSV:

```shell
uv run skeleton_dump --dir=src --ext=py,c,ts --out=-
```

Positional paths (file + directory):

```shell
uv run skeleton_dump src/uv_scripts/launch.py native/src --ext py,c
```

Include / exclude (quoted so the shell does not expand `*`):

```shell
uv run skeleton_dump --dir . --include 'src/**' --exclude '*_test.py,*.md'
```

Gitignored sources you still want outlined:

```shell
uv run skeleton_dump --dir . --include-gitignored 'vendor/**' --ext py
```

Extra ignore, no gitignore, hidden, follow symlinks:

```shell
uv run skeleton_dump --dir . --ignore-file extra.ignore --no-gitignore --hidden --follow-symlinks
```

Skip hidden files:

```shell
uv run skeleton_dump --dir . --no-hidden --ext py
```

One explicit file (dumped even if gitignored):

```shell
uv run skeleton_dump src/uv_scripts/launch.py --out -
```

`--no-imports` — drop `import` / `#include` / `require`:

```shell
uv run skeleton_dump --dir src --ext py --no-imports
```

`--no-signatures` — banners remain, `def` / `class` lines go:

```shell
uv run skeleton_dump --dir src --ext py --no-signatures
```

`--no-docs` — drop module/class/function docstrings and `/** */`:

```shell
uv run skeleton_dump --dir src --ext py --no-docs
```

`--no-comments` — drop `#` / `//` / `/* */`:

```shell
uv run skeleton_dump --dir src --ext py,c --no-comments
```

`--no-fields` — drop class fields and type-literal members:

```shell
uv run skeleton_dump --dir src --ext py,ts --no-fields
```

`--bodies` — keep function bodies instead of `...` / `{ ... }`:

```shell
uv run skeleton_dump --dir src --ext py --bodies
```

`--outline` — signatures + imports only (then `--docs` turns docs back on):

```shell
uv run skeleton_dump --dir src --ext py --outline
uv run skeleton_dump --dir src --ext py --outline --docs
```

`--no-collapse-objects` — keep `const cfg = { a: 1 }`:

```shell
uv run skeleton_dump --dir src --ext js --no-collapse-objects
```

`--collapse-control` — `if (x) { ... }`:

```shell
uv run skeleton_dump --dir src --ext c --collapse-control
```

`--focus` — full source for matching files, skeleton for the rest:

```shell
uv run skeleton_dump --dir src --ext py --focus 'kept.py'
```

`--lang py` — only files with an unknown extension (a `script` with no suffix):

```shell
uv run skeleton_dump script --lang py
```
