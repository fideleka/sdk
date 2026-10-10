"""Compile production shared Wi-Fi code; local HAL, no network or firmware build."""

from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
tests = root / "tests/wifi"
sources = root / "lib/lilka/src/lilka"
with tempfile.TemporaryDirectory(prefix="sdk-wifi-test-") as directory:
    for name in ("host", "connection"):
        for signedness in ("-fsigned-char", "-funsigned-char"):
            for sanitized in (False, True):
                output = Path(directory) / name
                flags = (
                    ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie"]
                    if sanitized
                    else []
                )
                subprocess.run(
                    [
                        "g++",
                        "-std=c++11",
                        "-Wall",
                        "-Wextra",
                        "-Werror",
                        "-Wno-sign-compare",
                        signedness,
                        *flags,
                        "-I" + str(tests),
                        "-I" + str(root / "lib/lilka/src"),
                        str(sources / "wifi_credentials.cpp"),
                        *([str(sources / "wifi_connection.cpp")] if name == "connection" else []),
                        str(tests / (name + ".cpp")),
                        "-o",
                        str(output),
                    ],
                    check=True,
                )
                subprocess.run([str(output)], check=True, env={**os.environ, "ASAN_OPTIONS": "detect_leaks=1"})
