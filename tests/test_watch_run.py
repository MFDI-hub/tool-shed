from __future__ import annotations

import subprocess
import sys
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from pathlib import Path


def run_watch(binary: Path, cwd: Path, args: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(binary), *args],
        cwd=cwd,
        check=False,
        text=True,
        capture_output=True,
        encoding="utf-8",
    )


def test_once_runs_command(watch_run_bin: Path, work: Path) -> None:
    marker = work / "ran.txt"
    result = run_watch(
        watch_run_bin,
        work,
        [
            "--dir",
            str(work),
            "--once",
            "--",
            sys.executable,
            "-c",
            "open('ran.txt', 'w', encoding='utf-8').write('ok')",
        ],
    )
    assert result.returncode == 0, result.stderr
    assert marker.read_text(encoding="utf-8") == "ok"


def test_missing_command_fails(watch_run_bin: Path, work: Path) -> None:
    result = run_watch(watch_run_bin, work, ["--dir", str(work)])
    assert result.returncode == 2
    assert "missing command" in result.stderr
