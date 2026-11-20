# quiet_exec

Run a command, print a **shrunk** stdout+stderr, exit with the command’s
status. For AI agents that should see failures, not pass spam. This is not
[`watch_run`](watch_run.md) (no watch, output is filtered) and not a repo dump.

```text
quiet_exec [options] -- command [args...]
```

The command is required and must come after `--`. stdout and stderr are merged,
then filtered:

- Drop blank lines, `PASSED` / `SKIPPED`, standalone `ok` / `OK`, and pytest
  progress-dot lines.
- Collapse 3+ identical consecutive remaining lines to one line plus
  `... (N more)`.
- Always keep lines matching `fail`, `error`, `fatal`, `traceback`, `assert`,
  `panic`, `E `, `===`, or `--- FAIL` (case-insensitive where noted).
- Then `--tail N` on non-priority lines (priority lines always survive).
- `--max-bytes` truncates the kept buffer.

A small, documented filter — not a per-ecosystem wrapper set.

## Flags

| Flag | Default | What it does |
|---|---|---|
| `--tail N` | `80` | Keep this many non-priority lines (from the end). |
| `--max-bytes N` | unlimited | Truncate kept output to N bytes. |
| `--` | required | Everything after this is the command and its args. |
| `-h`, `--help` | | Print usage. |

## Examples

Pytest, keep failures:

```shell
uv run quiet_exec -- pytest -q
```

Any command:

```shell
uv run quiet_exec -- python -m compileall src
```

Shorter tail:

```shell
uv run quiet_exec --tail 20 -- pytest tests
```

Cap size:

```shell
uv run quiet_exec --max-bytes 8000 -- cargo test
```
