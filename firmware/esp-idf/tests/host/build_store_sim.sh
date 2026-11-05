#!/usr/bin/env bash
# Сборка хост-стенда af_store.c: build_store_sim.sh <out> ; MAIN=каталог main (по умолчанию ../../main), SEDMUT=sed-мутация af_store.c, BASELINE=1 - без новых API.
set -eu
cd "$(dirname "$0")"
OUT=${1:-/tmp/store_sim}; MAIN=${MAIN:-../../main}
T=$(mktemp -d); mkdir -p "$T/inc"
cp "$MAIN"/af_*.h "$MAIN"/af_store.c "$T/inc/"
if [ -n "${SEDMUT:-}" ]; then cp "$T/inc/af_store.c" "$T/orig.c"; sed -i "$SEDMUT" "$T/inc/af_store.c"; if cmp -s "$T/orig.c" "$T/inc/af_store.c"; then echo "MUTATION NOT APPLIED"; exit 3; fi; fi
DEF=""; [ -n "${BASELINE:-}" ] && DEF="-DBASELINE"
gcc -std=gnu11 -O1 -g -fsanitize=address,undefined -Wall -Wno-unused-function $DEF -I"$T/inc" -Ipfs_stubs -Istore_stubs -I. -o "$OUT" store_sim.c -lm
rm -rf "$T"; echo "built $OUT"
