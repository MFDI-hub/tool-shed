from __future__ import annotations

import shutil
import subprocess
from typing import TYPE_CHECKING

import pytest

if TYPE_CHECKING:
    from pathlib import Path


def run_rel(binary: Path, cwd: Path, args: list[str]) -> subprocess.CompletedProcess[str]:
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


def test_file_cone_imports_and_importers(related_bin: Path, work: Path) -> None:
    write(work / "a.py", "import b\n\ndef leaf():\n    return b.hub()\n")
    write(work / "b.py", "def hub():\n    return 1\n")
    result = run_rel(related_bin, work, ["--dir", str(work), "--ext", "py", "--file", "a.py"])
    assert result.returncode == 0, result.stderr
    assert "a.py" in result.stdout
    assert "b.py" in result.stdout

    reverse = run_rel(related_bin, work, ["--dir", str(work), "--ext", "py", "--file", "b.py"])
    assert reverse.returncode == 0, reverse.stderr
    assert "a.py" in reverse.stdout
    assert "b.py" in reverse.stdout

    only_imp = run_rel(
        related_bin,
        work,
        ["--dir", str(work), "--ext", "py", "--file", "a.py", "--direction", "imports"],
    )
    assert only_imp.returncode == 0, only_imp.stderr
    assert "b.py" in only_imp.stdout


def test_missing_seed_exits_2(related_bin: Path, work: Path) -> None:
    write(work / "a.py", "x = 1\n")
    result = run_rel(related_bin, work, ["--dir", str(work), "--ext", "py"])
    assert result.returncode == 2


def test_from_git_includes_import_cone(related_bin: Path, work: Path) -> None:
    write(work / "a.py", "import b\n\ndef leaf():\n    return 1\n")
    write(work / "b.py", "def hub():\n    return 1\n")
    git(work, "init")
    git(work, "config", "user.email", "t@t.test")
    git(work, "config", "user.name", "t")
    git(work, "add", "a.py", "b.py")
    git(work, "commit", "-m", "init")
    write(work / "a.py", "import b\n\ndef leaf():\n    return 2\n")
    result = run_rel(related_bin, work, ["--dir", str(work), "--ext", "py", "--from-git"])
    assert result.returncode == 0, result.stderr
    assert "a.py" in result.stdout
    assert "b.py" in result.stdout
