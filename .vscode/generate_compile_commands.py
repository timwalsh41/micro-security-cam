#!/usr/bin/env python3
"""
Generate compile_commands.json with entries for test/native/ files only.

The C/C++ extension uses compile database entries on a per-file basis:
  - Files WITH entries in compile_commands.json use those flags directly.
  - Files WITHOUT entries fall back to the active configuration's
    includePath/defines (i.e. the PlatformIO ESP32 config for src/ files).

This means src/ files get ESP32 IntelliSense automatically, and
test/native/ files get native test IntelliSense automatically —
with zero manual configuration switching.

Usage:
    1. pio run -e native -t compiledb   # generates base compile_commands.json
    2. python3 .vscode/generate_compile_commands.py   # adds test entries

Run after regenerating with `pio init --ide vscode` or after adding
new test files.
"""

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUTPUT = ROOT / ".vscode" / "compile_commands.json"

# Compiler and flags for native test files
COMPILER = "/usr/bin/g++"
FLAGS = (
    "-DUNIT_TESTING "
    "-DUNITY_INCLUDE_CONFIG_H "
    "-Iinclude "
    "-Itest "
    "-Itest/mocks/native_mocks "
    "-I.pio/libdeps/native/Unity/src "
    "-I.pio/build/native/unity_config"
)

# Test source files live under this directory
TEST_DIR = ROOT / "test" / "native"


def find_test_files():
    """Find all .cpp files under test/native/."""
    return sorted(TEST_DIR.rglob("*.cpp"))


def make_entry(filepath):
    """Create a compile_commands.json entry for a test file."""
    rel = filepath.relative_to(ROOT)
    return {
        "command": f"{COMPILER} -o /dev/null -c {FLAGS} {rel}",
        "directory": str(ROOT),
        "file": str(rel),
        "output": "",
    }


def main():
    entries = []

    # Add entries for test files only (src/ files are handled by
    # the PlatformIO config's includePath/defines via fallback)
    test_files = find_test_files()
    for tf in test_files:
        entries.append(make_entry(tf))

    with open(OUTPUT, "w") as f:
        json.dump(entries, f, indent=4)
        f.write("\n")

    print(f"Wrote {OUTPUT} with {len(entries)} test file entries")
    return 0


if __name__ == "__main__":
    sys.exit(main())
