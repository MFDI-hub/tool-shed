from __future__ import annotations

import subprocess
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from pathlib import Path


def run_slice(binary: Path, cwd: Path, args: list[str]) -> subprocess.CompletedProcess[str]:
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


SRC = "def foo():\n    return 1\n\ndef bar():\n    return 2\n"


def test_name_extracts_one_body(slice_bin: Path, work: Path) -> None:
    write(work / "mod.py", SRC)
    result = run_slice(slice_bin, work, [str(work / "mod.py"), "--name", "foo"])
    assert result.returncode == 0, result.stderr
    assert "def foo():" in result.stdout
    assert "return 1" in result.stdout
    assert "def bar" not in result.stdout
    assert "return 2" not in result.stdout


def test_lines_range(slice_bin: Path, work: Path) -> None:
    write(work / "mod.py", SRC)
    result = run_slice(slice_bin, work, [str(work / "mod.py"), "--lines", "1-2"])
    assert result.returncode == 0, result.stderr
    body = result.stdout.split("# file:", 1)[1]
    body = body.split("\n", 1)[1].lstrip("\n")
    assert body == "def foo():\n    return 1\n"


def test_missing_flags_exits_2(slice_bin: Path, work: Path) -> None:
    write(work / "mod.py", SRC)
    result = run_slice(slice_bin, work, [str(work / "mod.py")])
    assert result.returncode == 2


def test_missing_name_exits_1(slice_bin: Path, work: Path) -> None:
    write(work / "mod.py", SRC)
    result = run_slice(slice_bin, work, [str(work / "mod.py"), "--name", "nope"])
    assert result.returncode == 1
    assert "not found" in result.stderr
