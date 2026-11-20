from __future__ import annotations

import importlib
import os
from pathlib import Path

import pytest

launch_mod = importlib.import_module("uv_scripts.launch")
from uv_scripts.launch import (
    TOOLS,
    exe_name,
    find_binary,
    find_repo_root,
    host_triple,
    launch,
    resolve_binary,
)


def test_tools_lists_sync_chosen_files() -> None:
    assert "sync_chosen_files" in TOOLS


def test_exe_name_matches_platform() -> None:
    name = exe_name("sync_chosen_files")
    if os.name == "nt":
        assert name.endswith(".exe")
    else:
        assert name == "sync_chosen_files"


def test_host_triple_is_nonempty() -> None:
    triple = host_triple()
    assert "-" in triple
    assert any(token in triple for token in ("x86_64", "aarch64", "arm", "riscv64"))


def test_normalize_machine_aliases() -> None:
    assert launch_mod._normalize_machine("x64") == "x86_64"
    assert launch_mod._normalize_machine("i686") == "x86"
    assert launch_mod._normalize_machine("armv6l") == "arm"
    assert launch_mod._normalize_machine("riscv64gc") == "riscv64"
    assert launch_mod._normalize_machine("ppc64le") == "powerpc64le"


def test_is_musl_returns_bool() -> None:
    assert isinstance(launch_mod._is_musl(), bool)


@pytest.mark.parametrize(
    ("system", "machine", "musl", "expected"),
    [
        ("Windows", "AMD64", False, "x86_64-windows-gnu"),
        ("Windows", "x86", False, "x86-windows-gnu"),
        ("Darwin", "arm64", False, "aarch64-macos"),
        ("Linux", "x86_64", False, "x86_64-linux-gnu.2.17"),
        ("Linux", "x86_64", True, "x86_64-linux-musl"),
        ("Linux", "i686", False, "x86-linux-gnu.2.17"),
        ("Linux", "armv7l", False, "arm-linux-gnueabihf"),
        ("Linux", "armv7l", True, "arm-linux-musleabihf"),
        ("Linux", "aarch64", True, "aarch64-linux-musl"),
        ("Linux", "riscv64", False, "riscv64-linux-gnu.2.27"),
        ("Linux", "ppc64le", True, "powerpc64le-linux-musl"),
        ("Linux", "s390x", False, "s390x-linux-gnu.2.17"),
        ("Linux", "loongarch64", False, "loongarch64-linux-gnu.2.36"),
        ("FreeBSD", "amd64", False, "x86_64-freebsd"),
    ],
)
def test_host_triple_variants(
    monkeypatch: pytest.MonkeyPatch, system: str, machine: str, musl: bool, expected: str
) -> None:
    monkeypatch.setattr(launch_mod.platform, "system", lambda: system)
    monkeypatch.setattr(launch_mod.platform, "machine", lambda: machine)
    monkeypatch.setattr(launch_mod, "_is_musl", lambda: musl)
    assert host_triple() == expected


def test_find_repo_root_from_this_tree() -> None:
    root = find_repo_root(Path(__file__).resolve())
    assert root is not None
    assert (root / "native" / "targets.toml").is_file()


def test_find_repo_root_missing() -> None:
    # A path outside this checkout: walking up must not find native/ + pyproject.toml.
    outside = Path("C:/") if os.name == "nt" else Path("/")
    assert find_repo_root(outside / "nowhere.py") is None


def test_find_binary_env_file(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    binary = tmp_path / exe_name("sync_chosen_files")
    binary.write_bytes(b"x")
    monkeypatch.setenv("UV_SCRIPTS_BIN", str(binary))
    found = find_binary("sync_chosen_files")
    assert found == binary


def test_find_binary_env_dir(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    binary = tmp_path / exe_name("sync_chosen_files")
    binary.write_bytes(b"x")
    monkeypatch.setenv("UV_SCRIPTS_BIN", str(tmp_path))
    assert find_binary("sync_chosen_files") == binary


def test_find_binary_packaged(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.delenv("UV_SCRIPTS_BIN", raising=False)
    native = tmp_path / "_native"
    native.mkdir()
    binary = native / exe_name("sync_chosen_files")
    binary.write_bytes(b"x")

    launch_file = tmp_path / "launch.py"
    launch_file.write_text("", encoding="utf-8")
    monkeypatch.setattr(launch_mod, "__file__", str(launch_file))
    monkeypatch.setattr(launch_mod, "find_repo_root", lambda start=None: None)
    assert find_binary("sync_chosen_files") == binary


def test_find_binary_dist(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.delenv("UV_SCRIPTS_BIN", raising=False)
    repo = tmp_path / "repo"
    triple_dir = repo / "dist" / "bin" / host_triple()
    triple_dir.mkdir(parents=True)
    binary = triple_dir / exe_name("sync_chosen_files")
    binary.write_bytes(b"x")
    (repo / "pyproject.toml").write_text("[project]\nname='x'\n", encoding="utf-8")
    (repo / "native").mkdir()

    packaged = tmp_path / "pkg" / "_native"
    packaged.mkdir(parents=True)
    launch_file = tmp_path / "pkg" / "launch.py"
    launch_file.write_text("", encoding="utf-8")
    monkeypatch.setattr(launch_mod, "__file__", str(launch_file))
    monkeypatch.setattr(launch_mod, "find_repo_root", lambda start=None: repo)
    assert find_binary("sync_chosen_files") == binary


def test_find_binary_missing(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.delenv("UV_SCRIPTS_BIN", raising=False)
    assert find_binary("definitely-not-a-tool") is None


def test_resolve_binary_missing(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(launch_mod, "find_binary", lambda tool: None)
    with pytest.raises(SystemExit, match="native binary"):
        resolve_binary("sync_chosen_files")


def test_launch_execv(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    binary = tmp_path / exe_name("sync_chosen_files")
    binary.write_bytes(b"x")
    monkeypatch.setattr(launch_mod, "resolve_binary", lambda tool: binary)
    monkeypatch.setattr(launch_mod.os, "name", "posix")
    called: list[list[str]] = []

    def fake_execv(path: str, args: list[str]) -> None:
        called.append([path, *args])

    monkeypatch.setattr(launch_mod.os, "execv", fake_execv)
    monkeypatch.setattr(launch_mod.sys, "argv", ["sync_chosen_files", "--once"])
    launch("sync_chosen_files")
    assert called[0][0] == str(binary)
    assert called[0][2] == "--once"


def test_launch_windows_subprocess(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    binary = tmp_path / "sync_chosen_files.exe"
    binary.write_bytes(b"x")
    monkeypatch.setattr(launch_mod, "resolve_binary", lambda tool: binary)
    monkeypatch.setattr(launch_mod.os, "name", "nt")
    monkeypatch.setattr(launch_mod.subprocess, "call", lambda args: 7)
    monkeypatch.setattr(launch_mod.sys, "argv", ["sync_chosen_files"])
    with pytest.raises(SystemExit) as exc:
        launch("sync_chosen_files")
    assert exc.value.code == 7
