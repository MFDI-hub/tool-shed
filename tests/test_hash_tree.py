from __future__ import annotations

import subprocess
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from pathlib import Path


def run_hash(binary: Path, cwd: Path, args: list[str]) -> subprocess.CompletedProcess[str]:
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


def test_hash_is_stable_and_changes_with_content(hash_tree_bin: Path, work: Path) -> None:
    write(work / "a.py", "a = 1\n")
    write(work / "b.py", "b = 2\n")
    first = run_hash(hash_tree_bin, work, ["--dir", str(work), "--ext", "py"])
    assert first.returncode == 0, first.stderr
    digest = first.stdout.strip()
    assert len(digest) == 64
    assert all(ch in "0123456789abcdef" for ch in digest)
    again = run_hash(hash_tree_bin, work, ["--dir", str(work), "--ext", "py"])
    assert again.returncode == 0, again.stderr
    assert again.stdout.strip() == digest
    write(work / "a.py", "a = 2\n")
    changed = run_hash(hash_tree_bin, work, ["--dir", str(work), "--ext", "py"])
    assert changed.returncode == 0, changed.stderr
    assert changed.stdout.strip() != digest
    assert len(changed.stdout.strip()) == 64
