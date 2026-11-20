"""Locate and run a bundled (or locally built) C tool binary."""

import os
import platform
import subprocess
import sys
import sysconfig
from pathlib import Path

TOOLS = (
    "sync_chosen_files",
    "watch_run",
    "skeleton_dump",
    "hash_tree",
    "repo_map",
    "related",
    "quiet_exec",
    "slice",
    "churn",
    "diff_syms",
)


def exe_name(tool):
    """Return the on-disk filename for `tool` on this OS."""
    return "{}.exe".format(tool) if os.name == "nt" else tool


def _normalize_machine(machine):
    name = machine.lower()
    if name in ("amd64", "x64", "x86_64"):
        return "x86_64"
    if name in ("i386", "i686", "x86"):
        return "x86"
    if name in ("arm64", "aarch64"):
        return "aarch64"
    if name in ("armv7l", "armv7", "armv6l"):
        return "arm"
    if name.startswith("arm") and "64" not in name:
        return "arm"
    if name.startswith("riscv64"):
        return "riscv64"
    if name in ("ppc64le", "powerpc64le"):
        return "powerpc64le"
    if name in ("ppc64", "powerpc64"):
        return "powerpc64"
    if name.startswith("loong"):
        return "loongarch64"
    return name


def _is_musl():
    for key in ("SOABI", "EXT_SUFFIX", "HOST_GNU_TYPE"):
        value = sysconfig.get_config_var(key)
        if value and "musl" in str(value):
            return True
    libc_ver = platform.libc_ver()
    return bool(libc_ver and libc_ver[0] and "musl" in libc_ver[0].lower())


def host_triple():
    """Zig target triple for the machine running this process."""
    system = platform.system().lower()
    cpu = _normalize_machine(platform.machine())
    if system == "windows":
        return "{}-windows-gnu".format(cpu)
    if system == "darwin":
        return "{}-macos".format(cpu)
    if system == "freebsd":
        return "{}-freebsd".format(cpu)
    if system == "linux":
        if _is_musl():
            if cpu == "arm":
                return "arm-linux-musleabihf"
            return "{}-linux-musl".format(cpu)
        if cpu == "arm":
            return "arm-linux-gnueabihf"
        if cpu == "riscv64":
            return "riscv64-linux-gnu.2.27"
        if cpu == "loongarch64":
            return "loongarch64-linux-gnu.2.36"
        if cpu == "powerpc64le":
            return "powerpc64le-linux-gnu.2.19"
        return "{}-linux-gnu.2.17".format(cpu)
    return "{}-{}".format(cpu, system)


def find_repo_root(start=None):
    """Walk up from `start` (or this file) looking for pyproject.toml + native/."""
    here = start if start is not None else Path(__file__).resolve()
    for path in [here] + list(here.parents):
        if (path / "pyproject.toml").is_file() and (path / "native").is_dir():
            return path
    return None


def _candidate_files(tool, directory):
    name = exe_name(tool)
    names = [name]
    if name.endswith(".exe"):
        names.append(tool)
    else:
        names.append("{}.exe".format(tool))
    return [directory / item for item in names]


def find_binary(tool):
    """Return the path to `tool`, or None if it is not on disk.

    Search order: ``UV_SCRIPTS_BIN``, packaged ``uv_scripts/_native/``,
    then ``dist/bin/<host-triple>/`` in a source checkout.
    """
    env = os.environ.get("UV_SCRIPTS_BIN")
    if env:
        override = Path(env).expanduser()
        if override.is_file():
            return override
        for candidate in _candidate_files(tool, override):
            if candidate.is_file():
                return candidate

    packaged_dir = Path(__file__).resolve().parent / "_native"
    for candidate in _candidate_files(tool, packaged_dir):
        if candidate.is_file():
            return candidate

    repo = find_repo_root()
    if repo is not None:
        dist_dir = repo / "dist" / "bin" / host_triple()
        for candidate in _candidate_files(tool, dist_dir):
            if candidate.is_file():
                return candidate
    return None


def resolve_binary(tool):
    """Return the path to `tool`, or raise SystemExit if it is missing."""
    found = find_binary(tool)
    if found is not None:
        return found
    hint = "uv run python scripts/build_native.py"
    raise SystemExit(
        "error: native binary for {0!r} not found.\n"
        "Build it with: {1}\n"
        "Or set UV_SCRIPTS_BIN to the directory (or file) that contains it.".format(tool, hint)
    )


def launch(tool, argv=None):
    """Run the C binary for `tool`, replacing this process when possible."""
    binary = resolve_binary(tool)
    extra = sys.argv[1:] if argv is None else argv
    args = [str(binary)] + list(extra)
    if os.name == "nt":
        raise SystemExit(subprocess.call(args))
    os.execv(args[0], args)
