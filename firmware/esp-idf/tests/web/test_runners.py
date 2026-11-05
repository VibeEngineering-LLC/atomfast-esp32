"""Regression checks for test infrastructure, without launching a browser."""
import io
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

import mock_server


class MockDispatchTests(unittest.TestCase):
    def test_get_and_post_use_ordered_first_match(self):
        for method in ("GET", "POST"):
            first = Mock()
            first.handle.return_value = False
            second = Mock()
            second.handle.return_value = True
            third = Mock()
            with patch.object(mock_server, "MOCK_HANDLERS", (first, second, third)):
                request = object()
                self.assertTrue(mock_server.dispatch(request, method, "/api", "a=1"))
                first.handle.assert_called_once_with(request, method, "/api", "a=1")
                second.handle.assert_called_once_with(request, method, "/api", "a=1")
                third.handle.assert_not_called()

    def test_unknown_get_and_post_keep_404_fallback(self):
        for method in ("GET", "POST"):
            request = object.__new__(mock_server.MockHandler)
            request.path = "/missing-route?a=1"
            request._send_text = Mock()
            request.rfile = io.BytesIO()
            getattr(request, "do_" + method)()
            request._send_text.assert_called_once_with("Not Found", "text/plain", 404)


class MutationRunnerTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="af_runner_fixture_")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        (self.root / "scripts").mkdir()
        (self.root / "web").mkdir()
        tests = self.root / "tests/web"
        tests.mkdir(parents=True)
        shutil.copy2(Path(__file__).with_name("mutate_e2e.py"), tests / "mutate_e2e.py")
        (self.root / "scripts/build_history.py").write_text(
            "from pathlib import Path\n"
            "if b'fail-build' in Path('web/feature.js').read_bytes(): raise SystemExit(3)\n"
            "Path('web/history.html').write_bytes(Path('web/feature.js').read_bytes())\n",
            encoding="utf-8")
        (self.root / "web/feature.js").write_bytes(b"const enabled = true;\r\n")
        (self.root / "web/history.html").write_bytes(b"original generated file\r\n")
        (tests / "mutations.txt").write_text(
            "feature.js | const enabled = true; => const enabled = false;\n", encoding="utf-8")
        (tests / "probe.py").write_text(
            "from pathlib import Path\n"
            "import json, os, sys, time\n"
            "assert Path.cwd() == Path(os.environ['AF_WEB_TEST_ROOT'])\n"
            "assert Path.cwd() != Path(os.environ['ORIGINAL_TEST_ROOT'])\n"
            "mode = sys.argv[1]\n"
            "mutated = 'false' in Path('web/history.html').read_text()\n"
            "if mode == 'timeout': time.sleep(5)\n"
            "if mode == 'exit': sys.exit(1)\n"
            "if mode == 'garbage': print('NO-RESULT'); sys.exit(0)\n"
            "total = 0 if mode == 'empty' else 1\n"
            "if mode == 'count' and mutated: total = 2\n"
            "failed = ['feature assertion'] if mode == 'baseline' or mutated and mode != 'survive' else []\n"
            "print(json.dumps({'total': total, 'failed': failed}))\n", encoding="utf-8")

    def run_case(self, mode, timeout=10, expected=0):
        before = {str(p.relative_to(self.root)): p.read_bytes()
                  for p in self.root.rglob("*") if p.is_file()}
        command = [sys.executable, str(self.root / "tests/web/mutate_e2e.py"), "mutations.txt",
                   "--timeout", str(timeout), "--runner", sys.executable, "tests/web/probe.py", mode]
        result = subprocess.run(command, cwd=self.root,
                                env=dict(os.environ, PYTHONDONTWRITEBYTECODE="1",
                                         ORIGINAL_TEST_ROOT=str(self.root)),
                                capture_output=True, encoding="utf-8", timeout=15)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        after = {str(p.relative_to(self.root)): p.read_bytes()
                 for p in self.root.rglob("*") if p.is_file()}
        self.assertEqual(before, after)
        return result

    def test_mutation_is_isolated_and_killed_by_completed_assertion(self):
        result = self.run_case("normal")
        self.assertIn("KILLED by feature assertion", result.stdout)
        self.assertIn("baseline: 1 assertions passed", result.stdout)

    def test_survivor_fails_acceptance(self):
        self.assertIn("SURVIVED", self.run_case("survive", expected=1).stdout)

    def test_baseline_and_runner_failures_are_infrastructure_errors(self):
        for mode in ("baseline", "exit", "garbage", "empty", "count"):
            with self.subTest(mode=mode):
                result = self.run_case(mode, expected=2)
                self.assertIn("INFRA:", result.stderr)
                self.assertNotIn("KILLED", result.stdout)

    def test_timeout_is_not_a_killed_mutation(self):
        result = self.run_case("timeout", timeout=0.2, expected=2)
        self.assertIn("INFRA:", result.stderr)
        self.assertNotIn("KILLED", result.stdout)

    def test_mutation_path_cannot_escape_web(self):
        path = self.root / "tests/web/mutations.txt"
        path.write_text("../scripts/build_history.py | from pathlib => import pathlib", encoding="utf-8")
        self.assertIn("escapes web", self.run_case("normal", expected=2).stderr)

    def test_failed_mutation_build_is_infrastructure_error(self):
        path = self.root / "tests/web/mutations.txt"
        path.write_text("feature.js | const enabled = true; => const enabled = 'fail-build';", encoding="utf-8")
        result = self.run_case("normal", expected=2)
        self.assertIn("INFRA:", result.stderr)
        self.assertNotIn("KILLED", result.stdout)


if __name__ == "__main__":
    unittest.main()
