"""Native target matrix sanity checks."""

from __future__ import annotations

import tomllib
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]


def test_every_target_has_a_unique_wheel_tag() -> None:
    data = tomllib.loads((REPO / "native" / "targets.toml").read_text(encoding="utf-8"))
    rows = data["target"]
    triples = [row["triple"] for row in rows]
    tags = [row["wheel_tag"] for row in rows]
    assert len(triples) == len(set(triples)), triples
    assert len(tags) == len(set(tags)), tags
    # Windows, macOS, manylinux, musllinux, FreeBSD — not a toy subset.
    assert len(rows) >= 20
    joined = " ".join(tags)
    for needed in (
        "win32",
        "win_amd64",
        "win_arm64",
        "macosx_11_0_arm64",
        "manylinux_2_17_x86_64",
        "manylinux_2_17_i686",
        "manylinux_2_17_ppc64le",
        "manylinux_2_17_s390x",
        "manylinux_2_36_loongarch64",
        "musllinux_1_2_x86_64",
        "freebsd_12_x86_64",
    ):
        assert needed in joined
