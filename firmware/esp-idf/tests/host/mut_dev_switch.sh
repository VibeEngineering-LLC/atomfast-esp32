#!/bin/bash
# Mutation check for af_core_dev_switch_clear / af_core_ingest_g (T24): each mutant must turn test_af_dev_switch red.
cd "$(dirname "$0")" || exit 1
W=/tmp/mut_dev_switch; rm -rf $W; mkdir -p $W
H=../../main/af_store_core.h
run() { # name sed-expr
  rm -rf $W/m; mkdir -p $W/m; cp -r ../../main $W/m/main; mkdir -p $W/m/tests; cp -r . $W/m/tests/host
  [ -n "$2" ] && sed -i "$2" $W/m/main/af_store_core.h
  if [ -n "$2" ] && cmp -s $H $W/m/main/af_store_core.h; then echo "$1: SED-NOOP"; return; fi
  ( cd $W/m/tests/host && cc -std=c11 -O1 -I../../main -I. -o t test_af_dev_switch.c -lm 2>/dev/null && ./t >out.txt 2>&1 ); rc=$?
  echo "$1: rc=$rc fails=$(grep -c '^FAIL' $W/m/tests/host/out.txt)"
}
run orig ""
run M56-no-gen-bump 's/    c->gen++;//'
run M57-no-win-reset '/lock_fn(ctx);/{n;s/    af_core_win_reset(c);//}'
run M58-mask-raw-only 's/AF_CLR_MASK_RAW | AF_CLR_MASK_AGG/AF_CLR_MASK_RAW/'
run M59-clear-after-reset 's/    int rc = clear_fn(AF_CLR_MASK_RAW | AF_CLR_MASK_AGG, ctx);/    int rc = 0;/;s/^    return rc;$/    rc = clear_fn(AF_CLR_MASK_RAW | AF_CLR_MASK_AGG, ctx); return rc;/'
run M60-no-gen-filter 's/    if (item_gen != c->gen) return 0;//'
run M61-win-reset-after-return-early 's/if (!c || !clear_fn || !lock_fn || !unlock_fn) return -1;/if (!c) return -1;/'
