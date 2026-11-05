#!/usr/bin/env python3
"""Run the same web source, bundle and regression checks locally and in CI."""
import os
from pathlib import Path
import subprocess
import sys


def main():
    root = Path(__file__).resolve().parent.parent
    tests = sorted(str(p.relative_to(root)) for p in (root / "tests/web").glob("*.test.js"))
    if not tests:
        sys.exit("No web regression tests found")
    commands = [
        [sys.executable, "scripts/build_history.py", "--check"],
        ["node", "tests/web/check-bundle.js"],
        ["node", "--test", "--test-reporter=dot", *tests],
        [sys.executable, "-m", "unittest", "discover", "-s", "tests/web", "-p", "test_*.py"],
    ]
    env = dict(os.environ, PYTHONUTF8="1", PYTHONDONTWRITEBYTECODE="1")
    for command in commands:
        result = subprocess.run(command, cwd=root, env=env)
        if result.returncode:
            return result.returncode
    return 0


if __name__ == "__main__":
    sys.exit(main())
