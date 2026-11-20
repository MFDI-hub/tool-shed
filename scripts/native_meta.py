"""Load native/targets.toml and native/tools.toml, and detect the host triple."""

from __future__ import annotations

import platform
import sys
import sysconfig
import tomllib
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
TARGETS_TOML = REPO_ROOT / "native" / "targets.toml"
TOOLS_TOML = REPO_ROOT / "native" / "tools.toml"
NATIVE_SRC = REPO_ROOT / "native" / "src"
DIST_BIN = REPO_ROOT / "dist" / "bin"


@dataclass(frozen=True)
class Target:
    triple: str
    wheel_tag: str
    exe: bool
    static: bool

    def binary_name(self, tool: str) -> str:
        return f"{tool}.exe" if self.exe else tool

    def output_dir(self) -> Path:
        return DIST_BIN / self.triple


@dataclass(frozen=True)
class Tool:
    name: str
    sources: tuple[Path, ...]


def load_zig_version() -> str:
    data = tomllib.loads(TARGETS_TOML.read_text(encoding="utf-8"))
    return str(data.get("zig_version", "0.17.1"))


def load_targets() -> list[Target]:
    data = tomllib.loads(TARGETS_TOML.read_text(encoding="utf-8"))
    return [
        Target(
            triple=str(row["triple"]),
            wheel_tag=str(row["wheel_tag"]),
            exe=bool(row["exe"]),
            static=bool(row["static"]),
        )
        for row in data["target"]
    ]


def load_tools() -> list[Tool]:
    data = tomllib.loads(TOOLS_TOML.read_text(encoding="utf-8"))
    tools: list[Tool] = []
    for row in data["tool"]:
        sources = tuple((REPO_ROOT / src).resolve() for src in row["sources"])
        tools.append(Tool(name=str(row["name"]), sources=sources))
    return tools


def shared_sources() -> list[Path]:
    return sorted(p for p in NATIVE_SRC.glob("*.c") if p.is_file())


def normalize_machine(machine: str) -> str:
    name = machine.lower()
    if name in {"amd64", "x64", "x86_64"}:
        return "x86_64"
    if name in {"i386", "i686", "x86"}:
        return "x86"
    if name in {"arm64", "aarch64"}:
        return "aarch64"
    if name in {"armv7l", "armv7", "armv6l"}:
        return "arm"
    if name.startswith("arm") and "64" not in name:
        return "arm"
    if name.startswith("riscv64"):
        return "riscv64"
    if name in {"ppc64le", "powerpc64le"}:
        return "powerpc64le"
    if name in {"ppc64", "powerpc64"}:
        return "powerpc64"
    if name.startswith("loong"):
        return "loongarch64"
    return name


def is_musl() -> bool:
    for key in ("SOABI", "EXT_SUFFIX", "HOST_GNU_TYPE"):
        value = sysconfig.get_config_var(key)
        if value and "musl" in str(value):
            return True
    return "musl" in platform.libc_ver()[0].lower()


def host_triple() -> str:
    system = platform.system().lower()
    cpu = normalize_machine(platform.machine())
    if system == "windows":
        return f"{cpu}-windows-gnu"
    if system == "darwin":
        return f"{cpu}-macos"
    if system == "freebsd":
        return f"{cpu}-freebsd"
    if system == "linux":
        if is_musl():
            if cpu == "arm":
                return "arm-linux-musleabihf"
            return f"{cpu}-linux-musl"
        if cpu == "arm":
            return "arm-linux-gnueabihf"
        if cpu == "riscv64":
            return "riscv64-linux-gnu.2.27"
        if cpu == "loongarch64":
            return "loongarch64-linux-gnu.2.36"
        if cpu == "powerpc64le":
            return "powerpc64le-linux-gnu.2.19"
        return f"{cpu}-linux-gnu.2.17"
    return f"{cpu}-{system}"


def host_target(targets: list[Target] | None = None) -> Target:
    triple = host_triple()
    known = targets if targets is not None else load_targets()
    for target in known:
        if target.triple == triple:
            return target
    exe = sys.platform == "win32"
    return Target(triple=triple, wheel_tag="any", exe=exe, static=exe)


def binary_path(tool: str, target: Target | None = None) -> Path:
    chosen = target if target is not None else host_target()
    return chosen.output_dir() / chosen.binary_name(tool)
