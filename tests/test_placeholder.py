"""Placeholder tests.

Delete these once you have real tests; they exist so that a freshly generated
project passes `make test` including its coverage gate.
"""

from __future__ import annotations

import uv_scripts
from uv_scripts import uv_scripts as cli


def test_version_is_a_string() -> None:
    assert isinstance(uv_scripts.__version__, str)


def test_main_runs() -> None:
    assert cli.main() is None


def test_python_m_entrypoint() -> None:
    import importlib

    entry = importlib.import_module("uv_scripts.__main__")
    assert entry.main is cli.main
