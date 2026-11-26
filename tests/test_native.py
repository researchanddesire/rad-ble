"""Builds and runs the host-compiled C++ tests in tests/native.

They need ArduinoJson. Set ARDUINOJSON_INCLUDE to a directory containing
ArduinoJson.h (CI downloads the single-header release). Without it, the tests
are skipped locally; CI sets it, so they always run there.
"""
import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
NATIVE = ROOT / "tests" / "native"


class NativeTests(unittest.TestCase):
    def test_native_suites(self):
        include = os.environ.get("ARDUINOJSON_INCLUDE")
        compiler = shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
        if not include or compiler is None:
            self.skipTest("set ARDUINOJSON_INCLUDE and install a C++ compiler to run native tests")
        sources = sorted(NATIVE.glob("test_*.cpp"))
        self.assertTrue(sources, "no native test sources found")
        with tempfile.TemporaryDirectory() as build:
            for source in sources:
                binary = pathlib.Path(build) / source.stem
                compiled = subprocess.run(
                    [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                     "-fsanitize=address,undefined", "-g",
                     "-I", str(ROOT / "src"), "-I", include,
                     str(source), str(ROOT / "src" / "RadBleResponse.cpp"),
                     "-o", str(binary)],
                    capture_output=True, text=True)
                self.assertEqual(compiled.returncode, 0, compiled.stderr)
                ran = subprocess.run([str(binary)], capture_output=True, text=True)
                self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)


if __name__ == "__main__":
    unittest.main()
