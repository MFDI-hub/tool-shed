## Installing uv and Python

This project is set up to use [**uv**](https://docs.astral.sh/uv/) for Python and
dependency management.

### Install uv

On macOS or Linux:

```shell
curl -LsSf https://astral.sh/uv/install.sh | sh
```

On Windows (PowerShell):

```powershell
powershell -ExecutionPolicy ByPass -c "irm https://astral.sh/uv/install.ps1 | iex"
```

If you use Homebrew (macOS):

```shell
brew update
brew install uv
```

See [uv's docs](https://docs.astral.sh/uv/getting-started/installation/) for all
supported installation methods.

### Install Python with uv

```shell
uv python install 3.13
```

The installed launcher supports Python 3.6 and newer. Development (lint, tests,
docs) still wants 3.11+. You can choose any compatible version.

### Install project dependencies

From your project root:

```shell
uv sync
```

This creates `.venv` and installs runtime + development dependencies.

### Install as a user tool

Published wheels are platform-specific (`py3-none-<platform>`) and bundle the C
binaries for that OS/CPU. After a release:

```shell
uv tool install tool-shed
sync_chosen_files --once
```

From a source checkout you must compile the host binary first (`zig` on `PATH`):

```shell
uv run python scripts/build_native.py
uv run sync_chosen_files --once
```
