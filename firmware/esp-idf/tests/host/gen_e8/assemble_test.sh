#!/usr/bin/env bash
# Сборка tests/host/test_af_push.c из gen_e8/push_t_*.inc: RUN-строки генерируются по именам static void t_*(void).
cd "$(dirname "$0")/.."
cp gen_e8/push_t_*.inc .
TESTINC=""
for f in push_t_cfg.inc push_t_logic.inc push_t_io.inc push_t_extra.inc; do [ -f "$f" ] && TESTINC="$TESTINC $f"; done
{
printf '%s\n' '/* Э8: host-тесты af_push.h. Сгенерировано gen_e8/assemble_test.sh из push_t_*.inc (тела тестов - gen_code.py). */' \
  '#pragma GCC diagnostic ignored "-Wunused-function"' '#include "af_test.h"' '#include <stdio.h>' '#include <stdlib.h>' \
  '#include <string.h>' '#include <math.h>' '#include <stdint.h>' '#include "af_push.h"' '#include "push_t_common.inc"'
for f in $TESTINC; do echo "#include \"$f\""; done
echo 'int main(void) {'
for f in $TESTINC; do
  grep -oE '^static void t_[a-z0-9_]+\(void\)' $f | sed -E 's/^static void (t_[a-z0-9_]+)\(void\)/    RUN(\1);/'
done
printf '%s\n' '    TESTS_DONE();' '}'
} > test_af_push.c
