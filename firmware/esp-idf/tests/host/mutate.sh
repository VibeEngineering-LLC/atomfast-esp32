#!/usr/bin/env bash
# Запуск таблицы мутантов: mutate.sh <таблица.mut> <заголовок.h>; MT_TARGET=test-<имя>.
# Строка таблицы (разделитель TAB): метка, ожидаемый красный тест, выражение sed. baseline = 0 FAIL (#SA-3).
set -u
cd "$(dirname "$0")"
. ./mutlib.sh
H=$2
mt_init "$H"
mt_base
while IFS=$'\t' read -r label expect expr; do
  [ -z "$label" ] && continue
  mt_mut "$label" "$expect" "$H" "$expr"
done < "$1"
mt_done
exit $MT_RC
