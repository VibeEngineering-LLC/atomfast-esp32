#!/usr/bin/env bash
# Проверка влияния добавленных реальных тестов (test_af_meas5.c) на таблицу meas.mut: какие FAIL даёт каждый мутант в meas5.
# Мутирует КОПИЮ main/*.h во временном каталоге, дерево не трогает. usage: mut_meas5_check.sh meas.mut af_meas.h
set -u
cd "$(dirname "$0")"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
while IFS=$'\t' read -r label expect expr; do
  [ -z "$label" ] && continue
  rm -rf "$T/m"; mkdir "$T/m"; cp ../../main/*.h "$T/m/"; sed -i "$expr" "$T/m/$2"
  if cmp -s "$T/m/$2" ../../main/$2; then echo "$label: SED_NOT_APPLIED"; continue; fi
  if gcc -std=c11 -O2 -I"$T/m" -I. -o "$T/m.bin" test_af_meas5.c -lm 2>/dev/null; then
    red=$(timeout 120 "$T/m.bin" 2>&1 | grep '^FAIL ' | awk '{print $2}' | sort -u | tr '\n' ' ')
    echo "$label: expected=$expect meas5_red=[${red}]"
  else echo "$label: BUILD_FAIL"; fi
done < "$1"
