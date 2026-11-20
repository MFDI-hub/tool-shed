from __future__ import annotations

import shutil
import subprocess
from typing import TYPE_CHECKING

import pytest

if TYPE_CHECKING:
    from pathlib import Path


def run_churn(binary: Path, cwd: Path, args: list[str]) -> subprocess.CompletedProcess[str]:
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


def test_ranks_hotter_file_first(churn_bin: Path, work: Path) -> None:
    write(work / "a.py", "x = 1\n")
    write(work / "b.py", "y = 1\n")
    git(work, "init")
    git(work, "config", "user.email", "t@t.test")
    git(work, "config", "user.name", "t")
    git(work, "add", "a.py", "b.py")
    git(work, "commit", "-m", "one")
    write(work / "a.py", "x = 2\n")
    git(work, "add", "a.py")
    git(work, "commit", "-m", "two")
    result = run_churn(churn_bin, work, ["--dir", str(work), "--ext", "py", "--days", "30"])
    assert result.returncode == 0, result.stderr
    assert result.stdout.index("a.py") < result.stdout.index("b.py")
    lines = [ln for ln in result.stdout.splitlines() if ln.endswith(".py")]
    assert lines[0].split()[0] == "2"
    assert "b.py" in lines[1] and lines[1].split()[0] == "1"
