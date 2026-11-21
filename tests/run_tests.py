"""Compile and run the transport regression tests with a host C++ compiler."""

import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--compiler", nargs=argparse.REMAINDER, default=["c++"],
        help="Compiler command, e.g. clang++ or /path/to/zig c++ (last option).",
    )
    args = parser.parse_args()
    if not args.compiler:
        parser.error("--compiler requires a compiler command")
    root = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix="modbustcp-tests-") as tmp:
        executable = Path(tmp) / "test_tcp.exe"
        subprocess.run(
            args.compiler + [
                "-std=c++14", "-Wall", "-Wextra", "-g",
                "-I" + str(root / "tests/stubs"),
                "-I" + str(root / "components/modbustcp"),
                str(root / "tests/test_tcp.cpp"),
                str(root / "components/modbustcp/modbus.cpp"),
                "-o", str(executable),
            ],
            check=True,
        )
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
