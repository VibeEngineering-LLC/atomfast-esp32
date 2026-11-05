#!/usr/bin/env bash
# Интеграционный хост-тест Э8 (внутри контейнера): эмулятор приёмника (настоящие listen.py + af_collector.push + MemoryDb) <-> клиент af_push.h по HTTP на 127.0.0.1.
# Запуск: bash tests/host/run_docker.sh 'bash /project/tests/host/run_integ.sh'
set -u
cd /project/tests/host || exit 9
CJ=/opt/esp/idf/components/json/cJSON
PORT=18480; TOKEN=integ-test-token-0123456789
gcc -std=c11 -Wall -Wextra -Werror -O2 -I../../main -I. -I$CJ ${INTEG_SAN:-} -o /tmp/integ test_af_push_integ.c $CJ/cJSON.c -lm || exit 3
export AF_LISTEN_DIR=/af_listen AF_SCRIPTS_DIR=/af_scripts PYTHONDONTWRITEBYTECODE=1
python3 -I -c "import sys" && python3 -B emu_receiver.py --port $PORT --token $TOKEN > /tmp/emu.log 2>&1 &
EMU=$!
for i in $(seq 1 50); do grep -q READY /tmp/emu.log 2>/dev/null && break; sleep 0.2; done
grep -q READY /tmp/emu.log || { echo "EMU NOT READY"; cat /tmp/emu.log; exit 4; }
timeout 300 /tmp/integ $PORT $TOKEN | grep -v '^PASS'
RC=${PIPESTATUS[0]}
kill $EMU 2>/dev/null
echo "INTEG_RC=$RC"; tail -5 /tmp/emu.log
