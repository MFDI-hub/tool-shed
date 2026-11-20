# uv-scripts

Portable tool for [uv](https://docs.astral.sh/uv/) scripts. The program is written in C and released as a build for each platform, so the same binary can be used everywhere. It will later be added to [python-template](https://github.com/MFDI-hub/python-template) as a tool.

## Status

This repository is scaffolded from [python-template](https://github.com/MFDI-hub/python-template). The C sources and the per-platform release builds come next.

## Development

Until the C build lands, this checkout uses the template's uv workflow.

```shell
uv sync
uv run pre-commit install && uv run pre-commit install --hook-type commit-msg
uv run pytest -n auto --cov
```

Commit `uv.lock`. CI runs `uv sync --locked` and fails when the lockfile is missing or stale.

*This project was built from [python-template](https://github.com/MFDI-hub/python-template).*
