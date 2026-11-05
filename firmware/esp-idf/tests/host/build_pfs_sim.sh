#!/usr/bin/env bash
# Сборка хост-стенда pfs_sim: build_pfs_sim.sh <flush_interval_s> <out>. Боевой af_persist_fs.c + настоящий LittleFS 2.11.
set -eu
cd "$(dirname "$0")"
N=${1:-300}; OUT=${2:-/tmp/pfs_sim_$N}
MAIN=${MAIN:-../../main}
LFS=../../managed_components/joltwallet__littlefs/src/littlefs
T=$(mktemp -d); mkdir -p "$T/inc"
cp "$MAIN"/af_*.h "$MAIN"/ttl_plan.h "$MAIN"/af_persist_fs.c "$MAIN"/af_log.c "$MAIN"/af_fsinfo.c "$T/inc/"
sed -i "s/#define AF_FLUSH_INTERVAL_S 300u/#define AF_FLUSH_INTERVAL_S ${N}u/" "$T/inc/af_persist.h"
if [ -n "${SEDMUT:-}" ]; then cp "$T/inc/${MUTFILE:-af_persist_fs.c}" "$T/orig.c"; sed -i "$SEDMUT" "$T/inc/${MUTFILE:-af_persist_fs.c}"; if cmp -s "$T/orig.c" "$T/inc/${MUTFILE:-af_persist_fs.c}"; then echo "MUTATION NOT APPLIED"; exit 3; fi; fi
gcc -std=gnu11 -O2 -g ${EXTRA_CFLAGS:-} -Wall -Wno-unused-function -Wno-unused-value -I"$T/inc" -Ipfs_stubs -I. -I"$LFS" -o "$OUT" pfs_sim.c "$T/inc/af_fsinfo.c" "$LFS/lfs.c" "$LFS/lfs_util.c" -lm
rm -rf "$T"; echo "built $OUT (flush ${N}s)"
