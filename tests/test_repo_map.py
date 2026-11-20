from __future__ import annotations

import subprocess
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from pathlib import Path


def run_map(binary: Path, cwd: Path, args: list[str]) -> subprocess.CompletedProcess[str]:
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


def test_import_rank_and_signatures(repo_map_bin: Path, work: Path) -> None:
    write(work / "a.py", "import b\n\ndef leaf():\n    return b.hub()\n")
    write(work / "b.py", "def hub():\n    return 1\n\nclass Core:\n    pass\n")
    out = work / "map.txt"
    result = run_map(repo_map_bin, work, ["--dir", str(work), "--ext", "py", "--out", str(out)])
    assert result.returncode == 0, result.stderr
    text = out.read_text(encoding="utf-8")
    assert "def leaf" in text
    assert "def hub" in text
    assert "class Core" in text
    assert text.index("b.py:") < text.index("a.py:")


def test_max_tokens_drops_later_files(repo_map_bin: Path, work: Path) -> None:
    for i in range(12):
        write(work / f"m{i:02d}.py", f"class M{i:02d}:\n    pass\n")
    out = work / "map.txt"
    result = run_map(
        repo_map_bin,
        work,
        ["--dir", str(work), "--ext", "py", "--max-tokens", "20", "--out", str(out)],
    )
    assert result.returncode == 0, result.stderr
    text = out.read_text(encoding="utf-8")
    assert "class M00" in text or "class M01" in text
    assert "class M11" not in text


def test_seed_pins_leaf(repo_map_bin: Path, work: Path) -> None:
    write(work / "hub.py", "def hub():\n    return 1\n")
    write(work / "a.py", "import hub\ndef leaf():\n    return hub.hub()\n")
    write(work / "zfill.py", "import hub\ndef extra():\n    return 0\n")
    out = work / "map.txt"
    no_seed = run_map(
        repo_map_bin,
        work,
        ["--dir", str(work), "--ext", "py", "--max-files", "1", "--out", str(out)],
    )
    assert no_seed.returncode == 0, no_seed.stderr
    text = out.read_text(encoding="utf-8")
    assert "hub.py:" in text
    assert "a.py:" not in text

    seeded = run_map(
        repo_map_bin,
        work,
        [
            "--dir",
            str(work),
            "--ext",
            "py",
            "--max-files",
            "1",
            "--seed",
            "a.py",
            "--out",
            str(out),
        ],
    )
    assert seeded.returncode == 0, seeded.stderr
    text = out.read_text(encoding="utf-8")
    assert "a.py:" in text
