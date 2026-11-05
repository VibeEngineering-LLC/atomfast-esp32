#!/usr/bin/env python3
"""Run browser mutations in a disposable project copy.

Usage: python tests/web/mutate_e2e.py mutations-e2e.txt --runner <command> [args...]

The runner executes in the copied project root. AF_WEB_TEST_ROOT points there,
and E2E selects the browser test (default ui-e2e.js). It must exit 0 and print
one JSON object with a positive total and a failed list of assertion names.
Runner failures, timeouts and malformed reports are infrastructure errors.
They never count as killed mutations. The browser launcher is supplied explicitly.
"""
import argparse
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


class InfrastructureError(RuntimeError):
    pass


def run(root, runner, timeout):
    env = dict(os.environ, PYTHONUTF8="1", PYTHONDONTWRITEBYTECODE="1",
               AF_WEB_TEST_ROOT=str(root), E2E=os.environ.get("E2E", "ui-e2e.js"))
    try:
        subprocess.run([sys.executable, str(root / "scripts/build_history.py")],
                       cwd=root, env=env, capture_output=True, check=True, timeout=timeout)
        result = subprocess.run(runner, cwd=root, env=env, capture_output=True,
                                text=True, encoding="utf-8", timeout=timeout)
    except (OSError, subprocess.SubprocessError) as error:
        raise InfrastructureError(str(error)) from error
    if result.returncode:
        raise InfrastructureError("runner exited %d: %s" % (result.returncode, result.stderr[:200]))
    try:
        report = json.loads(result.stdout)
    except (ValueError, TypeError) as error:
        raise InfrastructureError("runner did not return a JSON report") from error
    if not isinstance(report, dict):
        raise InfrastructureError("runner report must be an object")
    total, failed = report.get("total"), report.get("failed")
    if (type(total) is not int or total <= 0 or not isinstance(failed, list)
            or any(not isinstance(name, str) or not name for name in failed)
            or len(failed) > total):
        raise InfrastructureError("runner report needs total > 0 and a failed assertion list")
    return total, failed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mutations")
    parser.add_argument("--timeout", type=float, default=120)
    parser.add_argument("--runner", nargs=argparse.REMAINDER, required=True)
    args = parser.parse_args()
    if not args.runner or not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("a runner command and a positive timeout are required")
    source = Path(__file__).resolve().parent.parent.parent
    mutations = (source / "tests/web" / args.mutations).read_text(encoding="utf-8").splitlines()
    entries = [line for line in mutations if " | " in line and " => " in line]
    if not entries:
        parser.error("no mutations found")
    bad = 0
    try:
        with tempfile.TemporaryDirectory(prefix="af_e2e_") as directory:
            root = Path(directory)
            for name in ("web", "scripts", "tests/web"):
                shutil.copytree(source / name, root / name,
                                ignore=shutil.ignore_patterns("__pycache__"))
            total, failed = run(root, args.runner, args.timeout)
            if failed:
                raise InfrastructureError("baseline assertions failed: " + "; ".join(failed))
            print("baseline: %d assertions passed" % total)
            for line in entries:
                filename, rest = line.split(" | ", 1)
                before, after = rest.split(" => ", 1)
                path = (root / "web" / filename).resolve()
                if not path.is_relative_to(root / "web"):
                    raise InfrastructureError("mutation path escapes web")
                original = path.read_bytes()
                text = original.decode("utf-8")
                if text.count(before) != 1:
                    print("BAD-PATTERN", filename, before[:50], text.count(before))
                    bad += 1
                    continue
                try:
                    path.write_bytes(text.replace(before, after).encode("utf-8"))
                    count, failed = run(root, args.runner, args.timeout)
                    if count != total:
                        raise InfrastructureError("mutation changed assertion count")
                finally:
                    path.write_bytes(original)
                print("%s: %s" % (filename, "KILLED by " + "; ".join(failed) if failed else "SURVIVED"))
                bad += not failed
            print("mutations: %d, survived/bad: %d" % (len(entries), bad))
    except (InfrastructureError, OSError, UnicodeError) as error:
        print("INFRA:", error, file=sys.stderr)
        return 2
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
