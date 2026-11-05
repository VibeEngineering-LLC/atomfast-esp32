#!/usr/bin/env bash
# Сценарии af_store.c (e8 пп. 9, 11, 12). MAIN/BASELINE как в build_store_sim.sh.
set -u
cd "$(dirname "$0")"
bash build_store_sim.sh /tmp/store_sim >/tmp/store_sim.build 2>&1 || { tail -20 /tmp/store_sim.build; exit 9; }
rc=0
for s in p9_taskfail p9b_hook_guard p13_env_submit p11_drain p12_timeout p14_gen; do
    /tmp/store_sim "$s" || rc=1
done
exit "$rc"
