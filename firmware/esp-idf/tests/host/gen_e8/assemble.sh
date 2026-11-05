#!/usr/bin/env bash
# Сборка main/af_push.h из сгенерированных фрагментов A,B,C,D2 (gen_code.py + точечные правки в самих fragX.h).
cd "$(dirname "$0")"
{ cat fragA.h; echo; cat fragB.h; echo; sed '/^#endif$/d' fragC.h; echo; cat fragD2.h; echo; echo '#endif'; } > ../../../main/af_push.h
