"""Package smoke tests."""

from __future__ import annotations

import importlib

import pytest

import tool_shed
from tool_shed import cli
from tool_shed.launch import TOOLS


def test_version_is_a_string() -> None:
    assert isinstance(tool_shed.__version__, str)


def test_main_lists_tools(capsys: pytest.CaptureFixture[str]) -> None:
    cli.main()
    captured = capsys.readouterr()
    assert "tool-shed tools:" in captured.out
    for name in TOOLS:
        assert name in captured.out


def test_python_m_entrypoint() -> None:
    entry = importlib.import_module("tool_shed.__main__")
    assert entry.main is cli.main


@pytest.mark.parametrize("name", list(TOOLS))
def test_tool_mains_launch(monkeypatch: pytest.MonkeyPatch, name: str) -> None:
    mod = importlib.import_module("tool_shed." + name)
    called: list[str] = []
    monkeypatch.setattr(mod, "launch", lambda n: called.append(n))
    mod.main()
    assert called == [name]
