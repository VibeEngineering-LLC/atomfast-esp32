#!/usr/bin/env bash
# Мутации #SA-3 интеграционного теста Э8 (внутри контейнера): для каждой строки integ.mut.src - копия main/*.h, sed в af_push.h, сборка test_af_push_integ, прогон против эмулятора.
# Запуск: bash tests/host/run_docker.sh 'bash /project/tests/host/run_integ_mut.sh'
set -u
cd /project/tests/host || exit 9
CJ=/opt/esp/idf/components/json/cJSON
export AF_LISTEN_DIR=/af_listen AF_SCRIPTS_DIR=/af_scripts PYTHONDONTWRITEBYTECODE=1
while IFS= read -r line; do
  label=${line%%@@*}; expr=${line#*@@}; [ -z "$label" ] && continue
  rm -rf /tmp/mh && mkdir /tmp/mh && cp ../../main/*.h /tmp/mh/ && sed -i "$expr" /tmp/mh/af_push.h
  cmp -s /tmp/mh/af_push.h ../../main/af_push.h && { echo "$label: SED DID NOT APPLY"; continue; }
  gcc -std=c11 -O2 -Wall -Wextra -I/tmp/mh -I. -I$CJ -o /tmp/integ_m test_af_push_integ.c $CJ/cJSON.c -lm 2>/dev/null || { echo "$label: BUILD FAILED"; continue; }
  python3 -B emu_receiver.py --port 18490 --token integ-test-token-0123456789 > /tmp/emu_m.log 2>&1 &
  EMU=$!
  for i in $(seq 1 50); do grep -q READY /tmp/emu_m.log 2>/dev/null && break; sleep 0.2; done
  out=$(timeout 120 /tmp/integ_m 18490 integ-test-token-0123456789 | grep '^FAIL' | awk '{print $2}' | sort -u | tr '\n' ' ')
  kill $EMU 2>/dev/null; wait $EMU 2>/dev/null
  [ -n "$out" ] && echo "$label: KILLED by [$out]" || echo "$label: SURVIVED"
done < integ.mut.src
