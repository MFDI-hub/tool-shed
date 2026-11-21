from __future__ import annotations

import json
import shutil
import subprocess
import time
from typing import TYPE_CHECKING

import pytest

if TYPE_CHECKING:
    from pathlib import Path


def run_sync(binary: Path, cwd: Path, args: list[str]) -> subprocess.CompletedProcess[str]:
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


def dump_text(path: Path) -> str:
    return path.read_text(encoding="utf-8").replace("\\", "/")


def test_once_output_shape(sync_bin: Path, work: Path) -> None:
    src = work / "src"
    write(src / "alpha.py", "print('a')\n")
    write(src / "beta.py", "print('b')\n")
    out = work / "dump.txt"
    result = run_sync(
        sync_bin,
        work,
        [str(src), "--once", "--out", str(out), "--ext", "py"],
    )
    assert result.returncode == 0, result.stderr
    text = out.read_text(encoding="utf-8")
    assert text.startswith("# synced dump\n# files: 2\n")
    assert "FILE: src/alpha.py" in text.replace("\\", "/")
    assert "FILE: src/beta.py" in text.replace("\\", "/")
    assert "print('a')" in text
    assert "print('b')" in text
    assert "wrote " in result.stdout
    assert "=" * 80 in text


def test_file_order_is_relative_path(sync_bin: Path, work: Path) -> None:
    src = work / "pkg"
    write(src / "z.py", "z = 1\n")
    write(src / "a.py", "a = 1\n")
    write(src / "m.py", "m = 1\n")
    out = work / "dump.txt"
    result = run_sync(sync_bin, work, [str(src), "--once", "--out", str(out)])
    assert result.returncode == 0, result.stderr
    text = out.read_text(encoding="utf-8").replace("\\", "/")
    pos_a = text.index("FILE: pkg/a.py")
    pos_m = text.index("FILE: pkg/m.py")
    pos_z = text.index("FILE: pkg/z.py")
    assert pos_a < pos_m < pos_z


def test_sort_longest_lines_mode(sync_bin: Path, work: Path) -> None:
    src = work / "lines.txt"
    write(src, "short\nmedium line\nxy\n")
    out = work / "out.txt"
    result = run_sync(
        sync_bin,
        work,
        [str(src), "--once", "--out", str(out), "--mode", "lines", "--sort-longest", "--no-header"],
    )
    assert result.returncode == 0, result.stderr
    lines = [line for line in out.read_text(encoding="utf-8").splitlines() if line]
    assert lines == ["medium line", "short", "xy"]


def test_lines_keep_file_order_without_sort(sync_bin: Path, work: Path) -> None:
    src = work / "lines.txt"
    write(src, "zeta\nalpha\nmid\n")
    out = work / "out.txt"
    result = run_sync(
        sync_bin,
        work,
        [str(src), "--once", "--out", str(out), "--mode", "lines", "--no-header"],
    )
    assert result.returncode == 0, result.stderr
    assert out.read_text(encoding="utf-8").splitlines() == ["zeta", "alpha", "mid"]


def test_gitignore_skips_directory_scan(sync_bin: Path, work: Path) -> None:
    write(work / ".gitignore", "secret.py\nignored/\n")
    write(work / "keep.py", "keep = 1\n")
    write(work / "secret.py", "secret = 1\n")
    write(work / "ignored" / "nope.py", "nope = 1\n")
    out = work / "dump.txt"
    result = run_sync(sync_bin, work, ["--dir", str(work), "--once", "--out", str(out)])
    assert result.returncode == 0, result.stderr
    text = out.read_text(encoding="utf-8")
    assert "keep = 1" in text
    assert "secret = 1" not in text
    assert "nope = 1" not in text


def test_explicit_gitignored_file_is_included(sync_bin: Path, work: Path) -> None:
    write(work / ".gitignore", "secret.py\n")
    write(work / "secret.py", "secret = 1\n")
    out = work / "dump.txt"
    result = run_sync(
        sync_bin,
        work,
        [str(work / "secret.py"), "--once", "--out", str(out)],
    )
    assert result.returncode == 0, result.stderr
    assert "secret = 1" in out.read_text(encoding="utf-8")


def test_skips_binary_files(sync_bin: Path, work: Path) -> None:
    write(work / "ok.py", "ok = 1\n")
    (work / "blob.py").write_bytes(b"print('x')\x00oops")
    out = work / "dump.txt"
    result = run_sync(sync_bin, work, ["--dir", str(work), "--once", "--out", str(out)])
    assert result.returncode == 0, result.stderr
    assert "non-text" in result.stderr
    text = out.read_text(encoding="utf-8")
    assert "ok = 1" in text
    assert "oops" not in text


def test_invalid_utf8_is_skipped(sync_bin: Path, work: Path) -> None:
    write(work / "ok.py", "ok = 1\n")
    (work / "bad.py").write_bytes(b"ok = 1\n\xff\xfe")
    out = work / "dump.txt"
    result = run_sync(sync_bin, work, ["--dir", str(work), "--once", "--out", str(out)])
    assert result.returncode == 0, result.stderr
    assert "non-text" in result.stderr
    assert "ok = 1" in out.read_text(encoding="utf-8")


def test_concat_mode(sync_bin: Path, work: Path) -> None:
    write(work / "a.py", "A\n")
    out = work / "dump.txt"
    result = run_sync(
        sync_bin,
        work,
        ["--dir", str(work), "--once", "--out", str(out), "--mode", "concat"],
    )
    assert result.returncode == 0, result.stderr
    text = out.read_text(encoding="utf-8").replace("\\", "/")
    assert "=== a.py ===" in text
    assert text.strip().endswith("A")


def test_file_list(sync_bin: Path, work: Path) -> None:
    write(work / "one.py", "one = 1\n")
    listing = work / "chosen.txt"
    listing.write_text("one.py\n", encoding="utf-8")
    out = work / "dump.txt"
    result = run_sync(
        sync_bin,
        work,
        ["--file-list", str(listing), "--once", "--out", str(out)],
    )
    assert result.returncode == 0, result.stderr
    assert "one = 1" in out.read_text(encoding="utf-8")


def test_missing_file_list_exits(sync_bin: Path, work: Path) -> None:
    result = run_sync(
        sync_bin,
        work,
        ["--file-list", str(work / "missing.txt"), "--once"],
    )
    assert result.returncode != 0
    assert "File list not found" in result.stderr


def test_negation_reincludes_file(sync_bin: Path, work: Path) -> None:
    write(work / ".gitignore", "*\n!keep.py\n")
    write(work / "keep.py", "keep = 1\n")
    write(work / "drop.py", "drop = 1\n")
    out = work / "dump.txt"
    result = run_sync(sync_bin, work, ["--dir", str(work), "--once", "--out", str(out)])
    assert result.returncode == 0, result.stderr
    text = dump_text(out)
    assert "keep = 1" in text
    assert "drop = 1" not in text


def test_parent_dir_ignore_blocks_negation(sync_bin: Path, work: Path) -> None:
    write(work / ".gitignore", "ignored/\n!ignored/keep.py\n")
    write(work / "keep.py", "keep = 1\n")
    write(work / "ignored" / "keep.py", "secret = 1\n")
    out = work / "dump.txt"
    result = run_sync(sync_bin, work, ["--dir", str(work), "--once", "--out", str(out)])
    assert result.returncode == 0, result.stderr
    text = dump_text(out)
    assert "keep = 1" in text
    assert "secret = 1" not in text


def test_unignore_dir_then_file(sync_bin: Path, work: Path) -> None:
    write(work / ".gitignore", "*\n!ignored/\n!ignored/keep.py\n")
    write(work / "drop.py", "drop = 1\n")
    write(work / "ignored" / "keep.py", "keep = 1\n")
    write(work / "ignored" / "drop.py", "nope = 1\n")
    out = work / "dump.txt"
    result = run_sync(sync_bin, work, ["--dir", str(work), "--once", "--out", str(out)])
    assert result.returncode == 0, result.stderr
    text = dump_text(out)
    assert "keep = 1" in text
    assert "drop = 1" not in text
    assert "nope = 1" not in text


def test_nested_gitignore_last_match_wins(sync_bin: Path, work: Path) -> None:
    write(work / ".gitignore", "*.py\n")
    write(work / "sub" / ".gitignore", "!keep.py\n")
    write(work / "drop.py", "drop = 1\n")
    write(work / "sub" / "keep.py", "keep = 1\n")
    write(work / "sub" / "other.py", "other = 1\n")
    out = work / "dump.txt"
    result = run_sync(sync_bin, work, ["--dir", str(work), "--once", "--out", str(out)])
    assert result.returncode == 0, result.stderr
    text = dump_text(out)
    assert "keep = 1" in text
    assert "drop = 1" not in text
    assert "other = 1" not in text


def test_globstar_and_character_class(sync_bin: Path, work: Path) -> None:
    write(work / ".gitignore", "src/**/skip.py\n*.[oa]\nfoo.[ch]\n")
    write(work / "src" / "deep" / "skip.py", "skip = 1\n")
    write(work / "src" / "deep" / "keep.py", "keep = 1\n")
    write(work / "lib.o", "obj = 1\n")
    write(work / "foo.c", "c = 1\n")
    write(work / "ok.py", "ok = 1\n")
    out = work / "dump.txt"
    result = run_sync(
        sync_bin,
        work,
        ["--dir", str(work), "--once", "--out", str(out), "--ext", "py,o,c"],
    )
    assert result.returncode == 0, result.stderr
    text = dump_text(out)
    assert "keep = 1" in text
    assert "ok = 1" in text
    assert "skip = 1" not in text
    assert "obj = 1" not in text
    assert "c = 1" not in text


def test_info_exclude(sync_bin: Path, work: Path) -> None:
    write(work / ".git" / "info" / "exclude", "hidden.py\n")
    write(work / "hidden.py", "hidden = 1\n")
    write(work / "keep.py", "keep = 1\n")
    out = work / "dump.txt"
    result = run_sync(sync_bin, work, ["--dir", str(work), "--once", "--out", str(out)])
    assert result.returncode == 0, result.stderr
    text = dump_text(out)
    assert "keep = 1" in text
    assert "hidden = 1" not in text


def test_core_excludesfile(sync_bin: Path, work: Path, home: Path) -> None:
    write(home / ".gitconfig", "[core]\n\texcludesFile = ~/extra.ignore\n")
    write(home / "extra.ignore", "from_global.py\n")
    write(work / "from_global.py", "global = 1\n")
    write(work / "keep.py", "keep = 1\n")
    out = work / "dump.txt"
    result = run_sync(sync_bin, work, ["--dir", str(work), "--once", "--out", str(out)])
    assert result.returncode == 0, result.stderr
    text = dump_text(out)
    assert "keep = 1" in text
    assert "global = 1" not in text


def test_explicit_dir_dumps_gitignored_folder(sync_bin: Path, work: Path) -> None:
    write(work / ".gitignore", "ignored/\nnested/\n")
    write(work / "ignored" / "keep.py", "keep = 1\n")
    write(work / "ignored" / "nested" / "nope.py", "nope = 1\n")
    out = work / "dump.txt"
    result = run_sync(
        sync_bin,
        work,
        ["--dir", str(work / "ignored"), "--once", "--out", str(out)],
    )
    assert result.returncode == 0, result.stderr
    text = dump_text(out)
    assert "keep = 1" in text
    assert "nope = 1" not in text


def test_no_gitignore_includes_ignored_files(sync_bin: Path, work: Path) -> None:
    write(work / ".gitignore", "secret.py\n")
    write(work / "secret.py", "secret = 1\n")
    write(work / "keep.py", "keep = 1\n")
    out = work / "dump.txt"
    result = run_sync(
        sync_bin,
        work,
        ["--dir", str(work), "--once", "--out", str(out), "--no-gitignore"],
    )
    assert result.returncode == 0, result.stderr
    text = dump_text(out)
    assert "secret = 1" in text
    assert "keep = 1" in text


def test_lines_mode_walks_directory(sync_bin: Path, work: Path) -> None:
    write(work / "a.py", "alpha\n")
    write(work / "b.py", "beta\n")
    out = work / "out.txt"
    result = run_sync(
        sync_bin,
        work,
        ["--dir", str(work), "--once", "--out", str(out), "--mode", "lines", "--no-header"],
    )
    assert result.returncode == 0, result.stderr
    assert dump_text(out).splitlines() == ["alpha", "beta"]


def test_default_dumps_every_text_file(sync_bin: Path, work: Path) -> None:
    write(work / "a.py", "py = 1\n")
    write(work / "notes.md", "md = 1\n")
    write(work / "app.ts", "ts = 1\n")
    write(work / "noext", "plain = 1\n")
    out = work / "dump.txt"
    result = run_sync(sync_bin, work, ["--dir", str(work), "--once", "--out", str(out)])
    assert result.returncode == 0, result.stderr
    text = dump_text(out)
    assert "py = 1" in text
    assert "md = 1" in text
    assert "ts = 1" in text
    assert "plain = 1" in text


def test_ext_keeps_only_listed_suffixes(sync_bin: Path, work: Path) -> None:
    write(work / "a.py", "py = 1\n")
    write(work / "notes.md", "md = 1\n")
    write(work / "app.ts", "ts = 1\n")
    out = work / "dump.txt"
    result = run_sync(
        sync_bin,
        work,
        ["--dir", str(work), "--once", "--out", str(out), "--ext", "md,ts"],
    )
    assert result.returncode == 0, result.stderr
    text = dump_text(out)
    assert "md = 1" in text
    assert "ts = 1" in text
    assert "py = 1" not in text


def test_skips_output_file_inside_tree(sync_bin: Path, work: Path) -> None:
    write(work / "keep.py", "keep = 1\n")
    out = work / "dump.py"
    write(out, "old dump\n")
    result = run_sync(sync_bin, work, ["--dir", str(work), "--once", "--out", str(out)])
    assert result.returncode == 0, result.stderr
    text = dump_text(out)
    assert "keep = 1" in text
    assert "old dump" not in text


@pytest.mark.skipif(shutil.which("git") is None, reason="git not installed")
def test_matches_git_check_ignore(sync_bin: Path, work: Path) -> None:
    git = shutil.which("git")
    assert git is not None
    subprocess.run([git, "init", "-q"], cwd=work, check=True)
    write(work / ".gitignore", "secret.py\nignored/\n*.tmp\nbuild/**\n!keep.tmp\n")
    write(work / "keep.py", "keep = 1\n")
    write(work / "secret.py", "secret = 1\n")
    write(work / "keep.tmp", "tmpkeep = 1\n")
    write(work / "drop.tmp", "tmpdrop = 1\n")
    write(work / "ignored" / "nope.py", "nope = 1\n")
    write(work / "build" / "deep" / "out.py", "out = 1\n")
    files = [
        work / "keep.py",
        work / "secret.py",
        work / "keep.tmp",
        work / "drop.tmp",
        work / "ignored" / "nope.py",
        work / "build" / "deep" / "out.py",
    ]
    ignored = set()
    tracked = set()
    for path in files:
        rel = path.relative_to(work).as_posix()
        proc = subprocess.run(
            [git, "check-ignore", "-q", rel],
            cwd=work,
            check=False,
        )
        if proc.returncode == 0:
            ignored.add(rel)
        else:
            tracked.add(rel)
    out = work / "dump.txt"
    result = run_sync(
        sync_bin,
        work,
        ["--dir", str(work), "--once", "--out", str(out)],
    )
    assert result.returncode == 0, result.stderr
    text = dump_text(out)
    for rel in tracked:
        assert f"FILE: {rel}" in text
    for rel in ignored:
        assert f"FILE: {rel}" not in text


def test_include_and_exclude_globs(sync_bin: Path, work: Path) -> None:
    write(work / "src" / "app.ts", "app = 1\n")
    write(work / "src" / "app.test.ts", "test = 1\n")
    write(work / "lib" / "util.ts", "util = 1\n")
    out = work / "dump.txt"
    result = run_sync(
        sync_bin,
        work,
        [
            "--dir",
            str(work),
            "--once",
            "--out",
            str(out),
            "--include",
            "src/**",
            "--exclude",
            "*.test.ts",
        ],
    )
    assert result.returncode == 0, result.stderr
    text = dump_text(out)
    assert "app = 1" in text
    assert "test = 1" not in text
    assert "util = 1" not in text


def test_include_gitignored_pattern(sync_bin: Path, work: Path) -> None:
    write(work / ".gitignore", "secret.py\nkeep.py\n")
    write(work / "secret.py", "secret = 1\n")
    write(work / "keep.py", "keep = 1\n")
    write(work / "other.py", "other = 1\n")
    out = work / "dump.txt"
    result = run_sync(
        sync_bin,
        work,
        [
            "--dir",
            str(work),
            "--once",
            "--out",
            str(out),
            "--include-gitignored",
            "secret.py",
        ],
    )
    assert result.returncode == 0, result.stderr
    text = dump_text(out)
    assert "secret = 1" in text
    assert "keep = 1" not in text
    assert "other = 1" in text


def test_syncignore_and_ignore_file(sync_bin: Path, work: Path) -> None:
    write(work / ".syncignore", "skip.md\n")
    write(work / "extra.ignore", "skip2.md\n")
    write(work / "keep.py", "keep = 1\n")
    write(work / "skip.md", "skip = 1\n")
    write(work / "skip2.md", "skip2 = 1\n")
    out = work / "dump.txt"
    result = run_sync(
        sync_bin,
        work,
        [
            "--dir",
            str(work),
            "--once",
            "--out",
            str(out),
            "--ignore-file",
            str(work / "extra.ignore"),
        ],
    )
    assert result.returncode == 0, result.stderr
    text = dump_text(out)
    assert "keep = 1" in text
    assert "skip = 1" not in text
    assert "skip2 = 1" not in text


def test_no_hidden_skips_dotfiles(sync_bin: Path, work: Path) -> None:
    write(work / "keep.py", "keep = 1\n")
    write(work / ".hidden.py", "hidden = 1\n")
    out = work / "dump.txt"
    hidden = run_sync(sync_bin, work, ["--dir", str(work), "--once", "--out", str(out)])
    assert hidden.returncode == 0, hidden.stderr
    assert "hidden = 1" in dump_text(out)
    result = run_sync(
        sync_bin,
        work,
        ["--dir", str(work), "--once", "--out", str(out), "--no-hidden"],
    )
    assert result.returncode == 0, result.stderr
    text = dump_text(out)
    assert "keep = 1" in text
    assert "hidden = 1" not in text


def test_secret_filenames_skipped_unless_allowed(sync_bin: Path, work: Path) -> None:
    write(work / "keep.py", "keep = 1\n")
    write(work / ".env", "SECRET=1\n")
    write(work / "credentials.json", '{"k": 1}\n')
    write(work / "key.pem", "-----BEGIN KEY-----\n")
    out = work / "dump.txt"
    blocked = run_sync(sync_bin, work, ["--dir", str(work), "--once", "--out", str(out)])
    assert blocked.returncode == 0, blocked.stderr
    text = dump_text(out)
    assert "keep = 1" in text
    assert "SECRET=1" not in text
    assert '"k": 1' not in text
    assert "BEGIN KEY" not in text
    allowed = run_sync(
        sync_bin,
        work,
        ["--dir", str(work), "--once", "--out", str(out), "--allow-secrets"],
    )
    assert allowed.returncode == 0, allowed.stderr
    text = dump_text(out)
    assert "SECRET=1" in text
    assert '"k": 1' in text
    assert "BEGIN KEY" in text


def test_list_and_dry_run(sync_bin: Path, work: Path) -> None:
    write(work / ".gitignore", "secret.py\n")
    write(work / "keep.py", "keep = 1\n")
    write(work / "secret.py", "secret = 1\n")
    listed = run_sync(sync_bin, work, ["--dir", str(work), "--list"])
    assert listed.returncode == 0, listed.stderr
    assert "+ keep.py" in listed.stdout.replace("\\", "/")
    assert "secret.py" not in listed.stdout or "- " in listed.stdout
    dry = run_sync(sync_bin, work, ["--dir", str(work), "--dry-run"])
    assert dry.returncode == 0, dry.stderr
    assert "+ keep.py" in dry.stdout.replace("\\", "/")
    assert "gitignore" in dry.stdout


def test_stdout_and_markdown_json_cxml(sync_bin: Path, work: Path) -> None:
    write(work / "a.py", "print(1)\n")
    out = run_sync(
        sync_bin,
        work,
        ["--dir", str(work), "--once", "--out", "-", "--mode", "markdown", "--no-tree"],
    )
    assert out.returncode == 0, out.stderr
    assert "## a.py" in out.stdout.replace("\\", "/")
    assert "```py" in out.stdout
    js = run_sync(
        sync_bin,
        work,
        ["--dir", str(work), "--once", "--out", "-", "--mode", "json", "--no-header"],
    )
    assert js.returncode == 0, js.stderr
    payload = json.loads(js.stdout.strip())
    assert payload["items"][0]["path"].replace("\\", "/").endswith("a.py")
    xml = run_sync(
        sync_bin,
        work,
        ["--dir", str(work), "--once", "--out", "-", "--mode", "cxml"],
    )
    assert xml.returncode == 0, xml.stderr
    assert "<documents>" in xml.stdout
    assert "<source>" in xml.stdout


def test_line_numbers_and_stats(sync_bin: Path, work: Path) -> None:
    write(work / "a.py", "one\ntwo\n")
    out = work / "dump.txt"
    result = run_sync(
        sync_bin,
        work,
        ["--dir", str(work), "--once", "--out", str(out), "--line-numbers"],
    )
    assert result.returncode == 0, result.stderr
    text = dump_text(out)
    assert "# files: 1" in text
    assert "# bytes:" in text
    assert "# tokens: ~" in text
    assert "     1| one" in text
    assert "     2| two" in text
    assert "# tree:" in text


def test_max_files_and_max_file_bytes(sync_bin: Path, work: Path) -> None:
    write(work / "a.py", "aaaa\n")
    write(work / "b.py", "b\n")
    out = work / "dump.txt"
    limited = run_sync(
        sync_bin,
        work,
        ["--dir", str(work), "--once", "--out", str(out), "--max-files", "1"],
    )
    assert limited.returncode == 0, limited.stderr
    text = dump_text(out)
    listed = [line for line in text.replace("\\", "/").splitlines() if line.startswith("FILE: ")]
    assert listed in (["FILE: a.py"], ["FILE: b.py"])
    skipped = run_sync(
        sync_bin,
        work,
        ["--dir", str(work), "--once", "--out", str(out), "--max-file-bytes", "2"],
    )
    assert skipped.returncode == 0, skipped.stderr
    text = dump_text(out)
    assert "aaaa" not in text
    truncated = run_sync(
        sync_bin,
        work,
        [
            "--dir",
            str(work),
            "--once",
            "--out",
            str(out),
            "--max-file-bytes",
            "2",
            "--truncate",
        ],
    )
    assert truncated.returncode == 0, truncated.stderr
    assert "[truncated]" in dump_text(out)


def test_append_and_diff(sync_bin: Path, work: Path) -> None:
    write(work / "a.py", "a = 1\n")
    out = work / "dump.txt"
    first = run_sync(sync_bin, work, ["--dir", str(work), "--once", "--out", str(out)])
    assert first.returncode == 0, first.stderr
    time.sleep(0.05)
    write(work / "b.py", "b = 1\n")
    diffed = run_sync(
        sync_bin,
        work,
        ["--dir", str(work), "--once", "--out", str(out), "--diff"],
    )
    assert diffed.returncode == 0, diffed.stderr
    text = dump_text(out)
    assert "b = 1" in text
    assert "a = 1" not in text
    run_sync(sync_bin, work, ["--dir", str(work), "--once", "--out", str(out)])
    appended = run_sync(
        sync_bin,
        work,
        ["--dir", str(work), "--once", "--out", str(out), "--append", "--no-header", "--no-tree"],
    )
    assert appended.returncode == 0, appended.stderr
    body = dump_text(out)
    assert body.count("FILE: a.py") >= 2


def test_date_filters(sync_bin: Path, work: Path) -> None:
    write(work / "a.py", "a = 1\n")
    out = work / "dump.txt"
    old = run_sync(
        sync_bin,
        work,
        ["--dir", str(work), "--once", "--out", str(out), "--older-than", "1999-01-01"],
    )
    assert old.returncode == 0, old.stderr
    assert "a = 1" not in dump_text(out)
    new = run_sync(
        sync_bin,
        work,
        ["--dir", str(work), "--once", "--out", str(out), "--newer-than", "1999-01-01"],
    )
    assert new.returncode == 0, new.stderr
    assert "a = 1" in dump_text(out)


def test_config_file_and_also_outputs(sync_bin: Path, work: Path) -> None:
    write(work / "a.py", "py = 1\n")
    write(work / "b.md", "md = 1\n")
    write(
        work / ".sync_chosen_files",
        "--dir .\n--once\n--out all.txt\n--also\n--out py.txt\n--ext py\n",
    )
    result = run_sync(sync_bin, work, [])
    assert result.returncode == 0, result.stderr
    all_text = dump_text(work / "all.txt")
    py_text = dump_text(work / "py.txt")
    assert "py = 1" in all_text
    assert "md = 1" in all_text
    assert "py = 1" in py_text
    assert "md = 1" not in py_text


def test_chunk_bytes(sync_bin: Path, work: Path) -> None:
    write(work / "a.py", "A" * 40 + "\n")
    write(work / "b.py", "B" * 40 + "\n")
    out = work / "dump.txt"
    result = run_sync(
        sync_bin,
        work,
        [
            "--dir",
            str(work),
            "--once",
            "--out",
            str(out),
            "--no-header",
            "--no-tree",
            "--chunk-bytes",
            "80",
        ],
    )
    assert result.returncode == 0, result.stderr
    parts = list(work.glob("dump-*.txt"))
    assert len(parts) >= 2
