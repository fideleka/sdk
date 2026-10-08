#!/usr/bin/env python3
"""Compile the real menu.cpp and Menu declaration against recording host graphics."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--source", type=Path, default=ROOT / "lib/lilka/src/lilka/menu.cpp")
parser.add_argument("--sanitize", action="store_true")
args = parser.parse_args()
ui = (ROOT / "lib/lilka/src/lilka/ui.h").read_text()
# Keep the production declaration (including defaults and private state), not a test copy.
menu = ui[ui.index("typedef void (*PMenuItemCallback)"):ui.index("/// Клас для відображення сповіщення.")]
with tempfile.TemporaryDirectory(prefix="lilka-menu-test-") as tmp:
    tmp = Path(tmp)
    (tmp / "ui.h").write_text(
        """#include "mock_graphics.h"
namespace lilka {
""" + menu + """
}
"""
    )
    (tmp / "menu.cpp").write_text(args.source.read_text())
    command = [os.environ.get("CXX", "g++"), "-std=c++17", "-Wall", "-Wextra",
               "-Wno-sign-compare", "-Werror=sequence-point", "-I", str(tmp), "-I", str(ROOT / "tests/menu"),
               str(tmp / "menu.cpp"), str(ROOT / "tests/menu/regression.cpp"),
               "-o", str(tmp / "regression")]
    if args.sanitize:
        command[1:1] = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g"]
    subprocess.run(command, check=True)
    subprocess.run([str(tmp / "regression")], check=True)
