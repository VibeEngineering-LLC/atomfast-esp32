#!/bin/bash
# Таблица мутантов заголовков: mut_tbl.sh <таблица> ; строка (разделитель @): файл, sed-выражение, метка, тест (файл в main/, тест = имя бинаря без test_af_).
# Печатает красные тесты; "SED DID NOT APPLY" - шаблон устарел (ошибка таблицы); "CRASH" - тест упал аварийно (мутант убит); "ВЫЖИЛ" - ни один тест не покраснел.
cd "$(dirname "$0")" || exit 9
M=../../main; T=$(mktemp -d)
while IFS='@' read -r f expr name t; do
  [ -z "$f" ] && continue
  cp "$M/$f" "$T/orig"; sed -i -E "$expr" "$M/$f"
  if cmp -s "$M/$f" "$T/orig"; then echo "$name: SED DID NOT APPLY"; continue; fi
  CJ=""; [ "$t" = "sens_req" ] && CJ="-I${CJSON_DIR:-/opt/esp/idf/components/json/cJSON} ${CJSON_DIR:-/opt/esp/idf/components/json/cJSON}/cJSON.c"
  if cc -std=c11 -O2 -Wall -Wextra -I"$M" -I. $CJ -o "$T/mt" "test_af_$t.c" -lm 2>"$T/err"; then
    out=$(timeout 60 "$T/mt" 2>&1); rc=$?
    red=$(echo "$out" | grep -E '^FAIL ' | awk '{print $2}' | sort -u | tr '\n' ' ')
    if [ -n "$red" ]; then echo "$name: $red"; elif [ $rc -ne 0 ]; then echo "$name: CRASH rc=$rc"; else echo "$name: ВЫЖИЛ"; fi
  else echo "$name: НЕ СОБРАЛСЯ"; fi
  cp "$T/orig" "$M/$f"
done < "$1"
rm -rf "$T"
