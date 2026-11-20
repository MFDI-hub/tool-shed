from __future__ import annotations

import subprocess
import sys
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from pathlib import Path


def run_quiet(binary: Path, args: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(binary), *args],
        check=False,
        text=True,
        capture_output=True,
        encoding="utf-8",
    )


def test_keeps_failed_drops_ok_spam(quiet_exec_bin: Path) -> None:
    script = "import sys\n" + ("print('ok')\n" * 30) + "print('FAILED tests/x.py')\nsys.exit(1)\n"
    result = run_quiet(quiet_exec_bin, ["--", sys.executable, "-c", script])
    assert result.returncode == 1
    assert "FAILED tests/x.py" in result.stdout
    assert result.stdout.count("ok") <= 1


def test_preserves_zero_exit(quiet_exec_bin: Path) -> None:
    result = run_quiet(quiet_exec_bin, ["--", sys.executable, "-c", "print('hello')"])
    assert result.returncode == 0
    assert "hello" in result.stdout


def test_missing_command_exits_2(quiet_exec_bin: Path) -> None:
    result = run_quiet(quiet_exec_bin, [])
    assert result.returncode == 2
