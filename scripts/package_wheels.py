#!/usr/bin/env python3
"""Stage host-matching binaries into the package and emit platform wheels."""

from __future__ import annotations

import argparse
import base64
import hashlib
import shutil
import subprocess
import tempfile
import zipfile
from pathlib import Path

from native_meta import REPO_ROOT, Target, binary_path, load_targets, load_tools

NATIVE_PKG = REPO_ROOT / "src" / "tool_shed" / "_native"
DIST = REPO_ROOT / "dist"


def file_digest(path: Path) -> tuple[str, int]:
    data = path.read_bytes()
    digest = hashlib.sha256(data).digest()
    return base64.urlsafe_b64encode(digest).rstrip(b"=").decode("ascii"), len(data)


def rewrite_wheel(src_wheel: Path, dest_wheel: Path, tag: str) -> None:
    dest_wheel.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as raw:
        root = Path(raw)
        with zipfile.ZipFile(src_wheel) as zf:
            zf.extractall(root)

        dist_infos = list(root.glob("*.dist-info"))
        if not dist_infos:
            raise SystemExit(f"error: no dist-info in {src_wheel}")
        dist_info = dist_infos[0]
        wheel_file = dist_info / "WHEEL"
        lines = wheel_file.read_text(encoding="utf-8").splitlines()
        rewritten: list[str] = []
        saw_tag = False
        for line in lines:
            if line.startswith("Tag:"):
                rewritten.append(f"Tag: {tag}")
                saw_tag = True
            else:
                rewritten.append(line)
        if not saw_tag:
            rewritten.append(f"Tag: {tag}")
        text = "\n".join(rewritten) + "\n"
        wheel_file.write_text(text, encoding="utf-8")

        record_path = dist_info / "RECORD"
        records: list[str] = []
        for path in sorted(root.rglob("*")):
            if not path.is_file():
                continue
            rel = path.relative_to(root).as_posix()
            if path.name == "RECORD" and path.parent == dist_info:
                records.append(f"{rel},,")
                continue
            digest, size = file_digest(path)
            records.append(f"{rel},sha256={digest},{size}")
        record_path.write_text("\n".join(records) + "\n", encoding="utf-8")

        if dest_wheel.exists():
            dest_wheel.unlink()
        with zipfile.ZipFile(dest_wheel, "w", compression=zipfile.ZIP_DEFLATED) as zf:
            for path in sorted(root.rglob("*")):
                if path.is_file():
                    zf.write(path, path.relative_to(root).as_posix())


def clear_staged_binaries() -> None:
    NATIVE_PKG.mkdir(parents=True, exist_ok=True)
    for path in NATIVE_PKG.iterdir():
        if path.name in {".gitkeep", ".gitignore"}:
            continue
        if path.is_file():
            path.unlink()


def stage_binaries(target: Target) -> list[Path]:
    clear_staged_binaries()
    staged: list[Path] = []
    for tool in load_tools():
        src = binary_path(tool.name, target)
        if not src.is_file():
            raise SystemExit(f"error: missing binary {src} (run scripts/build_native.py --all)")
        dest = NATIVE_PKG / src.name
        shutil.copy2(src, dest)
        staged.append(dest)
    return staged


def uv_build_wheel(out_dir: Path) -> Path:
    out_dir.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        ["uv", "build", "--wheel", "--out-dir", str(out_dir)],
        cwd=REPO_ROOT,
        check=True,
    )
    wheels = list(out_dir.glob("*.whl"))
    if len(wheels) != 1:
        raise SystemExit(f"error: expected one wheel in {out_dir}, found {wheels}")
    return wheels[0]


def retag_name(wheel: Path, tag: str) -> str:
    # {name}-{version}-{python}-{abi}-{platform}.whl  (any -> platform)
    name = wheel.name
    if not name.endswith(".whl"):
        raise SystemExit(f"error: not a wheel: {wheel}")
    stem = name[: -len(".whl")]
    parts = stem.split("-")
    if len(parts) < 5:
        raise SystemExit(f"error: unexpected wheel name {name}")
    parts[-1] = tag
    parts[-2] = "none"
    parts[-3] = "py3"
    return "-".join(parts) + ".whl"


def package_target(target: Target, dist: Path) -> Path:
    print(f"packaging {target.triple} -> {target.wheel_tag}")
    stage_binaries(target)
    with tempfile.TemporaryDirectory() as raw:
        built = uv_build_wheel(Path(raw))
        dest = dist / retag_name(built, target.wheel_tag)
        rewrite_wheel(built, dest, f"py3-none-{target.wheel_tag}")
    return dest


def parse_args() -> argparse.Namespace:
    ap = argparse.ArgumentParser(description="Build platform wheels that bundle C binaries.")
    ap.add_argument(
        "--target",
        action="append",
        default=[],
        metavar="TRIPLE",
        help="Package this Zig triple (repeatable). Default: every built target.",
    )
    ap.add_argument(
        "--out-dir",
        default=str(DIST),
        help="Directory for platform wheels (default: dist/)",
    )
    ap.add_argument(
        "--assets-dir",
        default=None,
        help="Copy renamed binaries here (stem-triple[.exe]) for GitHub Releases",
    )
    return ap.parse_args()


def main() -> int:
    args = parse_args()
    dist = Path(args.out_dir)
    dist.mkdir(parents=True, exist_ok=True)
    targets = load_targets()
    if args.target:
        by_triple = {t.triple: t for t in targets}
        selected = []
        for triple in args.target:
            if triple not in by_triple:
                raise SystemExit(f"error: unknown target {triple}")
            selected.append(by_triple[triple])
    else:
        selected = [t for t in targets if t.output_dir().is_dir()]
        if not selected:
            raise SystemExit(
                "error: no dist/bin/<triple> directories; run scripts/build_native.py --all"
            )

    built: list[Path] = []
    try:
        for target in selected:
            built.append(package_target(target, dist))
    finally:
        clear_staged_binaries()

    assets_dir = Path(args.assets_dir) if args.assets_dir else None
    if assets_dir is not None:
        assets_dir.mkdir(parents=True, exist_ok=True)
        for target in selected:
            for tool in load_tools():
                src = binary_path(tool.name, target)
                if src.is_file():
                    dest = assets_dir / f"{src.stem}-{target.triple}{src.suffix}"
                    shutil.copy2(src, dest)
                    print(f"  asset {dest}")

    print(f"wrote {len(built)} wheel(s)")
    for path in built:
        print(f"  {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
