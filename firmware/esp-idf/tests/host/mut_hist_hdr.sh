#!/bin/sh
# #SA-3: test_af_hist_hdr must go red on (m1) the old 9-header set and (m2) a silently ignored set_hdr failure; green on the original.
cd "$(dirname "$0")" || exit 2
D=/tmp/af_mut_hdr; rm -rf $D; mkdir -p $D
CF="-std=c11 -Wall -Wextra -Werror -O2 -I$D -I../../main -I. -Ihttp_stub"
run() { gcc $CF -o $D/t_$1 test_af_hist_hdr.c || { echo "mutant $1 does not compile"; exit 3; }; $D/t_$1 >/dev/null 2>&1; }
H=../../main/af_history_hdr.h
run orig || { echo "orig NOT green"; exit 1; }
# m1: старый набор - Ring + отдельные Epoch/First-Seq/Lost, без бюджета (9 заголовков при device+ds)
sed -e 's/AF_HIST_HDR_BUDGET ||/99 ||/' \
    -e 's|AF_HH_SET("X-AF-Range", range);|AF_HH_SET("X-AF-Ring", hh->kind); AF_HH_SET("X-AF-Epoch", hh->epoch); AF_HH_SET("X-AF-First-Seq", hh->first_seq); AF_HH_SET("X-AF-Lost", hh->lost);|' $H > $D/af_history_hdr.h
run m1 && { echo "m1 (9 headers) NOT detected"; exit 1; } || echo "m1 red: ok"
# m2: возврат httpd_resp_set_hdr игнорируется (тихая потеря)
sed -e 's/ || httpd_resp_set_hdr(req, (k), (v)) != ESP_OK)/) { httpd_resp_set_hdr(req, (k), (v)); } if (0)/' $H > $D/af_history_hdr.h
run m2 && { echo "m2 (ignored rc) NOT detected"; exit 1; } || echo "m2 red: ok"
echo "mut_hist_hdr: all mutants killed"
