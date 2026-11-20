from __future__ import annotations

import shutil
import subprocess
from typing import TYPE_CHECKING

import pytest

if TYPE_CHECKING:
    from pathlib import Path


def run_ds(binary: Path, cwd: Path, args: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(binary), *args],
        cwd=cwd,
        check=False,
        text=True,
        capture_output=True,
        encoding="utf-8",
    )


def write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def git(cwd: Path, *args: str) -> None:
    exe = shutil.which("git")
    if not exe:
        pytest.skip("git not on PATH")
    result = subprocess.run(
        [exe, *args], cwd=cwd, check=False, text=True, capture_output=True, encoding="utf-8"
    )
    assert result.returncode == 0, result.stderr


def test_added_and_kept_signatures(diff_syms_bin: Path, work: Path) -> None:
    write(work / "mod.py", "def old():\n    return 1\n")
    git(work, "init")
    git(work, "config", "user.email", "t@t.test")
    git(work, "config", "user.name", "t")
    git(work, "add", "mod.py")
    git(work, "commit", "-m", "init")
    write(work / "mod.py", "def old():\n    return 1\n\ndef new():\n    return 2\n")
    result = run_ds(diff_syms_bin, work, ["--dir", str(work), "--ext", "py"])
    assert result.returncode == 0, result.stderr
    assert "+ def new" in result.stdout
    assert "= def old" in result.stdout
    assert "return 1" not in result.stdout
