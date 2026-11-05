cd /project/tests/host
make clean >/dev/null 2>&1
AF_DUMP_DIR=/dump/d1 make test > /tmp/t.log 2>&1; echo "TEST_RC=$?"
grep -E "^TESTS" /tmp/t.log | awk '{n+=$2; f+=$4} END {print "SUITES", NR, "TESTS", n, "FAILURES", f}'
grep -E "^FAIL|FAIL " /tmp/t.log | head -20
grep -E "error|Error" /tmp/t.log | head -5
make clean >/dev/null 2>&1
AF_DUMP_DIR=/dump/d2 make asan > /tmp/a.log 2>&1; echo "ASAN_RC=$?"
grep -E "^TESTS" /tmp/a.log | awk '{n+=$2; f+=$4} END {print "SUITES", NR, "TESTS", n, "FAILURES", f}'
echo "SANITIZER_MSGS: $(grep -ci 'runtime error\|AddressSanitizer\|LeakSanitizer' /tmp/a.log)"
make clean >/dev/null 2>&1
echo FINISHED
