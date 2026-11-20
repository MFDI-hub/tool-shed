from __future__ import annotations

import subprocess
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from pathlib import Path


def run_skel(binary: Path, cwd: Path, args: list[str]) -> subprocess.CompletedProcess[str]:
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


def test_python_keeps_signatures_drops_bodies(skeleton_bin: Path, work: Path) -> None:
    write(
        work / "mod.py",
        "import os\n"
        "\n"
        "def foo():\n"
        "    return 1\n"
        "\n"
        "class Bar:\n"
        "    def baz(self):\n"
        "        return 2\n",
    )
    out = work / "skel.txt"
    result = run_skel(skeleton_bin, work, ["--dir", str(work), "--ext", "py", "--out", str(out)])
    assert result.returncode == 0, result.stderr
    text = out.read_text(encoding="utf-8")
    assert "import os" in text
    assert "def foo():" in text
    assert "class Bar:" in text
    assert "def baz(self):" in text
    assert "    ..." in text
    assert "return 1" not in text
    assert "return 2" not in text


def test_python_keeps_docstrings_fields_and_type_checking(skeleton_bin: Path, work: Path) -> None:
    write(
        work / "mod.py",
        '"""Module doc."""\n'
        "from typing import TYPE_CHECKING\n"
        "\n"
        "FOO = 1\n"
        "skip_me = 2\n"
        "\n"
        "if TYPE_CHECKING:\n"
        "    from collections.abc import Mapping\n"
        "\n"
        "@dataclass\n"
        "class Item:\n"
        '    """An item."""\n'
        "    x: int\n"
        '    y: str = "a"\n'
        "    def meth(self) -> int:\n"
        '        """Return x."""\n'
        "        return self.x\n"
        "\n"
        "async def run(\n"
        "    n: int,\n"
        ") -> None:\n"
        "    await n\n",
    )
    out = work / "skel.txt"
    result = run_skel(skeleton_bin, work, ["--dir", str(work), "--ext", "py", "--out", str(out)])
    assert result.returncode == 0, result.stderr
    text = out.read_text(encoding="utf-8")
    assert "Module doc." in text
    assert "FOO = 1" in text
    assert "skip_me = 2" not in text
    assert "from collections.abc import Mapping" in text
    assert "@dataclass" in text
    assert "x: int" in text
    assert "y: str" in text
    assert "An item." in text
    assert "Return x." in text
    assert "async def run(" in text
    assert "n: int," in text
    assert ") -> None:" in text
    assert "return self.x" not in text
    assert "await n" not in text


def test_c_collapses_function_bodies(skeleton_bin: Path, work: Path) -> None:
    write(
        work / "lib.c",
        "#include <stdio.h>\n\nint foo(void) {\n    return 1;\n}\n",
    )
    out = work / "skel.txt"
    result = run_skel(skeleton_bin, work, ["--dir", str(work), "--ext", "c", "--out", str(out)])
    assert result.returncode == 0, result.stderr
    text = out.read_text(encoding="utf-8")
    assert "#include <stdio.h>" in text
    assert "int foo(void) { ... }" in text
    assert "return 1" not in text


def test_js_collapses_object_literals(skeleton_bin: Path, work: Path) -> None:
    write(
        work / "app.js",
        "const cfg = {\n    secret: 1,\n    nested: { x: 2 }\n};\nexport function f() {\n    return cfg;\n}\n",
    )
    out = work / "skel.txt"
    result = run_skel(skeleton_bin, work, ["--dir", str(work), "--ext", "js", "--out", str(out)])
    assert result.returncode == 0, result.stderr
    text = out.read_text(encoding="utf-8")
    assert "const cfg = { ... }" in text
    assert "secret: 1" not in text
    assert "export function f() { ... }" in text
    assert "return cfg" not in text


def test_ts_keeps_type_literal_collapses_object(skeleton_bin: Path, work: Path) -> None:
    write(
        work / "types.ts",
        "type Foo = { a: number }\nconst cfg = { a: 1 }\nfunction f() {\n    return cfg\n}\n",
    )
    out = work / "skel.txt"
    result = run_skel(skeleton_bin, work, ["--dir", str(work), "--ext", "ts", "--out", str(out)])
    assert result.returncode == 0, result.stderr
    text = out.read_text(encoding="utf-8")
    assert "type Foo = { a: number }" in text
    assert "const cfg = { ... }" in text
    assert "a: 1" not in text
    assert "function f() { ... }" in text


def test_no_collapse_objects_keeps_object_body(skeleton_bin: Path, work: Path) -> None:
    write(work / "app.js", "const cfg = { a: 1 }\nfunction f() { return 1 }\n")
    out = work / "skel.txt"
    result = run_skel(
        skeleton_bin,
        work,
        ["--dir", str(work), "--ext", "js", "--no-collapse-objects", "--out", str(out)],
    )
    assert result.returncode == 0, result.stderr
    text = out.read_text(encoding="utf-8")
    assert "a: 1" in text
    assert "function f() { ... }" in text
    assert "return 1" not in text


def test_collapse_control_collapses_if(skeleton_bin: Path, work: Path) -> None:
    write(
        work / "lib.c",
        "int foo(int x) {\n    if (x) {\n        return 1;\n    }\n    return 0;\n}\n",
    )
    out = work / "skel.txt"
    result = run_skel(skeleton_bin, work, ["--dir", str(work), "--ext", "c", "--out", str(out)])
    assert result.returncode == 0, result.stderr
    default = out.read_text(encoding="utf-8")
    assert "int foo(int x) { ... }" in default
    assert "if (x)" not in default

    write(work / "ctrl.c", "int x = 1;\nif (x) {\n    return 1;\n}\n")
    result = run_skel(
        skeleton_bin,
        work,
        ["--dir", str(work), "--include", "ctrl.c", "--collapse-control", "--out", str(out)],
    )
    assert result.returncode == 0, result.stderr
    text = out.read_text(encoding="utf-8")
    assert "if (x) { ... }" in text
    assert "return 1" not in text

    result = run_skel(
        skeleton_bin,
        work,
        ["--dir", str(work), "--include", "ctrl.c", "--out", str(out)],
    )
    assert result.returncode == 0, result.stderr
    kept = out.read_text(encoding="utf-8")
    assert "if (x) {" in kept
    assert "return 1;" in kept


def test_keep_flag_pairs(skeleton_bin: Path, work: Path) -> None:
    write(
        work / "mod.py",
        "# hdr\n"
        '"""Module doc."""\n'
        "import os\n"
        "FOO = 1\n"
        "class Item:\n"
        "    x: int\n"
        "    def meth(self):\n"
        '        """Return x."""\n'
        "        return self.x\n",
    )
    out = work / "skel.txt"

    def dump(*flags: str) -> str:
        result = run_skel(
            skeleton_bin,
            work,
            ["--dir", str(work), "--ext", "py", "--out", str(out), *flags],
        )
        assert result.returncode == 0, result.stderr
        return out.read_text(encoding="utf-8")

    no_docs = dump("--no-docs")
    assert "Module doc." not in no_docs
    assert "Return x." not in no_docs
    assert "def meth(self):" in no_docs
    assert "import os" in no_docs

    no_comments = dump("--no-comments")
    assert "# hdr" not in no_comments
    assert "import os" in no_comments

    no_fields = dump("--no-fields")
    assert "x: int" not in no_fields
    assert "FOO = 1" not in no_fields
    assert "class Item:" in no_fields

    no_sigs = dump("--no-signatures")
    assert "class Item:" not in no_sigs
    assert "def meth" not in no_sigs
    assert "import os" in no_sigs
    assert "FILE: mod.py" in no_sigs

    no_imp = dump("--no-imports")
    assert "import os" not in no_imp
    assert "class Item:" in no_imp

    outline = dump("--outline")
    assert "Module doc." not in outline
    assert "# hdr" not in outline
    assert "x: int" not in outline
    assert "FOO = 1" not in outline
    assert "import os" in outline
    assert "class Item:" in outline
    assert "def meth(self):" in outline
    assert "return self.x" not in outline

    outline_docs = dump("--outline", "--docs")
    assert "Module doc." in outline_docs
    assert "x: int" not in outline_docs

    bodies = dump("--bodies")
    assert "return self.x" in bodies

    empty = dump("--no-imports", "--no-signatures", "--no-docs", "--no-comments", "--no-fields")
    assert "FILE: mod.py" in empty
    assert "import os" not in empty
    assert "class Item:" not in empty


def test_focus_emits_full_matching_file(skeleton_bin: Path, work: Path) -> None:
    write(work / "kept.py", "def foo():\n    return 1\n")
    write(work / "other.py", "def bar():\n    return 2\n")
    out = work / "skel.txt"
    result = run_skel(
        skeleton_bin,
        work,
        ["--dir", str(work), "--ext", "py", "--focus", "kept.py", "--out", str(out)],
    )
    assert result.returncode == 0, result.stderr
    text = out.read_text(encoding="utf-8")
    assert "return 1" in text
    assert "def foo():" in text
    assert "def bar():" in text
    assert "return 2" not in text
    assert "    ..." in text


def test_lang_py_on_extensionless_file(skeleton_bin: Path, work: Path) -> None:
    write(work / "script", "def foo():\n    return 1\n")
    out = work / "skel.txt"
    result = run_skel(
        skeleton_bin,
        work,
        ["--dir", str(work), "--include", "script", "--lang", "py", "--out", str(out)],
    )
    assert result.returncode == 0, result.stderr
    text = out.read_text(encoding="utf-8")
    assert "def foo():" in text
    assert "    ..." in text
    assert "return 1" not in text


def test_shebang_python_without_extension(skeleton_bin: Path, work: Path) -> None:
    write(work / "runme", "#!/usr/bin/env python3\ndef foo():\n    return 1\n")
    out = work / "skel.txt"
    result = run_skel(
        skeleton_bin,
        work,
        ["--dir", str(work), "--include", "runme", "--out", str(out)],
    )
    assert result.returncode == 0, result.stderr
    text = out.read_text(encoding="utf-8")
    assert "def foo():" in text
    assert "return 1" not in text


def test_ruby_indent_skeleton(skeleton_bin: Path, work: Path) -> None:
    write(
        work / "app.rb",
        "require 'json'\n# note\nmodule M\n  class A\n    def foo\n      1\n    end\n  end\nend\n",
    )
    out = work / "skel.txt"
    result = run_skel(skeleton_bin, work, ["--dir", str(work), "--ext", "rb", "--out", str(out)])
    assert result.returncode == 0, result.stderr
    text = out.read_text(encoding="utf-8")
    assert "require 'json'" in text
    assert "# note" in text
    assert "module M" in text
    assert "class A" in text
    assert "def foo" in text
    assert "    ..." in text
    assert "      1" not in text
