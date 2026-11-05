#!/bin/bash
# Mutation check for ble_addr.h: each mutant must turn test_ble_addr red; original must be green.
cd "$(dirname "$0")" || exit 1
W=/tmp/mut_ble_addr; rm -rf $W; mkdir -p $W/main $W/tests/host
cp test_ble_addr.c af_test.h $W/tests/host/
run() { # name sed-expr
  cp ../../main/ble_addr.h $W/main/ble_addr.h
  [ -n "$2" ] && sed -i "$2" $W/main/ble_addr.h
  if [ -n "$2" ] && cmp -s ../../main/ble_addr.h $W/main/ble_addr.h; then echo "$1: SED-NOOP"; return; fi
  ( cd $W/tests/host && gcc -std=c11 -O1 -I../../main -I. -o t test_ble_addr.c 2>/dev/null && ./t >out.txt 2>&1 ); rc=$?
  echo "$1: rc=$rc fails=$(grep -c FAIL $W/tests/host/out.txt)"
}
run orig ""
run m1-len17-to-16 's/len != 17 \&\& len != 12/len != 16 \&\& len != 12/'
run m2-no-sep-check 's/if (s\[5\] != sep) return false;//'
run m3-allow-G 's/c <= .f./c <= '"'"'g'"'"'/'
run m4-write-out-early 's/tmp\[i\] = (uint8_t)((hi << 4) | lo);/out[i] = tmp[i] = (uint8_t)((hi << 4) | lo);/'
run m5-swap-nibbles 's/(hi << 4) | lo/(lo << 4) | hi/'
run m6-null-out 's/ || out == NULL//'
run m7-reverse-order 's/out\[i\] = tmp\[i\]/out[5 - i] = tmp[i]/'
