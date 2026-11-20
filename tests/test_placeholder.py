"""Package smoke tests."""

from __future__ import annotations

import importlib

import pytest

import uv_scripts
from uv_scripts import uv_scripts as cli
from uv_scripts.launch import TOOLS


def test_version_is_a_string() -> None:
    assert isinstance(uv_scripts.__version__, str)


def test_main_lists_tools(capsys: pytest.CaptureFixture[str]) -> None:
    cli.main()
    captured = capsys.readouterr()
    assert "tool-shed tools:" in captured.out
    for name in TOOLS:
        assert name in captured.out


def test_python_m_entrypoint() -> None:
    entry = importlib.import_module("uv_scripts.__main__")
    assert entry.main is cli.main


@pytest.mark.parametrize("name", list(TOOLS))
def test_tool_mains_launch(monkeypatch: pytest.MonkeyPatch, name: str) -> None:
    mod = importlib.import_module("uv_scripts." + name)
    called: list[str] = []
    monkeypatch.setattr(mod, "launch", lambda n: called.append(n))
    mod.main()
    assert called == [name]
