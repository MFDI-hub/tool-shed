#!/usr/bin/env python3
"""Compile C tools with Zig (`zig cc`) for the host target, or every target."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
from pathlib import Path

from native_meta import (
    REPO_ROOT,
    Target,
    Tool,
    binary_path,
    host_target,
    load_targets,
    load_tools,
    load_zig_version,
    shared_sources,
)

ZIG_INSTALL = "https://ziglang.org/download/"


def find_zig(explicit: str | None) -> str:
    if explicit:
        path = Path(explicit)
        if path.is_file():
            return str(path)
        raise SystemExit(f"error: zig not found at {explicit}")
    found = shutil.which("zig")
    if found:
        return found
    raise SystemExit(
        "error: zig not found on PATH.\n"
        f"Install Zig {load_zig_version()} from {ZIG_INSTALL} and ensure `zig` is on PATH."
    )


def zig_cc_cmd(
    zig: str,
    *,
    target: Target,
    tool: Tool,
    outfile: Path,
) -> list[str]:
    cmd = [
        zig,
        "cc",
        "-O2",
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-Wno-unused-parameter",
        f"-I{REPO_ROOT / 'native' / 'src'}",
        "-target",
        target.triple,
        "-o",
        str(outfile),
    ]
    if target.static:
        cmd.append("-static")
    if target.triple.endswith("-macos") or "-macos" in target.triple:
        cmd.append("-mmacosx-version-min=11.0")
    if "windows" in target.triple:
        cmd.extend(["-lshell32", "-lkernel32", "-D_CRT_SECURE_NO_WARNINGS"])
    elif "freebsd" not in target.triple:
        cmd.append("-D_XOPEN_SOURCE=700")
    cmd.extend(str(src) for src in tool.sources)
    cmd.extend(str(src) for src in shared_sources())
    return cmd


def build_one(zig: str, target: Target, tool: Tool) -> Path:
    out_dir = target.output_dir()
    out_dir.mkdir(parents=True, exist_ok=True)
    outfile = binary_path(tool.name, target)
    cmd = zig_cc_cmd(zig, target=target, tool=tool, outfile=outfile)
    print(f"-> {tool.name} ({target.triple})")
    env = os.environ.copy()
    result = subprocess.run(cmd, cwd=REPO_ROOT, env=env, check=False)
    if result.returncode != 0:
        raise SystemExit(f"error: zig cc failed for {tool.name} target {target.triple}")
    return outfile


def parse_args() -> argparse.Namespace:
    ap = argparse.ArgumentParser(description="Build tools-shed C tools with Zig.")
    ap.add_argument("--all", action="store_true", help="Build every target in native/targets.toml")
    ap.add_argument(
        "--target",
        action="append",
        default=[],
        metavar="TRIPLE",
        help="Build this Zig triple (repeatable). Default: host",
    )
    ap.add_argument("--zig", default=None, help="Path to the zig binary")
    ap.add_argument("--tool", action="append", default=[], help="Build only this tool (repeatable)")
    return ap.parse_args()


def main() -> int:
    args = parse_args()
    zig = find_zig(args.zig)
    targets = load_targets()
    tools = load_tools()
    if args.tool:
        wanted = set(args.tool)
        tools = [tool for tool in tools if tool.name in wanted]
        missing = wanted - {tool.name for tool in tools}
        if missing:
            raise SystemExit(f"error: unknown tool(s): {', '.join(sorted(missing))}")

    if args.all:
        selected = targets
    elif args.target:
        by_triple = {t.triple: t for t in targets}
        selected = []
        for triple in args.target:
            if triple not in by_triple:
                raise SystemExit(f"error: unknown target {triple}")
            selected.append(by_triple[triple])
    else:
        selected = [host_target(targets)]

    built: list[Path] = []
    for target in selected:
        for tool in tools:
            built.append(build_one(zig, target, tool))

    print(f"built {len(built)} binary(ies)")
    for path in built:
        print(f"  {path.relative_to(REPO_ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
