from __future__ import annotations

from typing import TYPE_CHECKING

import pytest

from tool_shed.launch import find_binary

if TYPE_CHECKING:
    from pathlib import Path


def require_bin(name: str) -> Path:
    path = find_binary(name)
    if path is None:
        pytest.skip("host binary missing; run: uv run python scripts/build_native.py")
    return path


@pytest.fixture(scope="session")
def sync_bin() -> Path:
    return require_bin("sync_chosen_files")


@pytest.fixture(scope="session")
def watch_run_bin() -> Path:
    return require_bin("watch_run")


@pytest.fixture(scope="session")
def skeleton_bin() -> Path:
    return require_bin("skeleton_dump")


@pytest.fixture(scope="session")
def hash_tree_bin() -> Path:
    return require_bin("hash_tree")


@pytest.fixture(scope="session")
def repo_map_bin() -> Path:
    return require_bin("repo_map")


@pytest.fixture(scope="session")
def related_bin() -> Path:
    return require_bin("related")


@pytest.fixture(scope="session")
def quiet_exec_bin() -> Path:
    return require_bin("quiet_exec")


@pytest.fixture(scope="session")
def slice_bin() -> Path:
    return require_bin("slice")


@pytest.fixture(scope="session")
def churn_bin() -> Path:
    return require_bin("churn")


@pytest.fixture(scope="session")
def diff_syms_bin() -> Path:
    return require_bin("diff_syms")


@pytest.fixture
def home(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> Path:
    """Isolated HOME so the developer's global excludesFile cannot hide test files."""
    isolated = tmp_path / "home"
    isolated.mkdir()
    monkeypatch.setenv("HOME", str(isolated))
    monkeypatch.setenv("USERPROFILE", str(isolated))
    monkeypatch.delenv("XDG_CONFIG_HOME", raising=False)
    monkeypatch.delenv("GIT_CONFIG_GLOBAL", raising=False)
    monkeypatch.delenv("GIT_CONFIG_SYSTEM", raising=False)
    monkeypatch.setenv("GIT_CONFIG_NOSYSTEM", "1")
    return isolated


@pytest.fixture
def work(tmp_path: Path, home: Path) -> Path:
    """Temp tree with its own git root so the repo .gitignore does not apply.

    HOME lives beside this tree (not inside it) so a default all-text dump does
    not pick up the isolated gitconfig.
    """
    assert home.is_dir()
    repo = tmp_path / "repo"
    repo.mkdir()
    (repo / ".git").mkdir()
    return repo
