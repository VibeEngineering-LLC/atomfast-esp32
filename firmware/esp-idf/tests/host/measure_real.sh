#!/usr/bin/env bash
# #SA-3a: для каждого мутанта числом назвать, на скольких строках боевого вывода зонда мутант и исправный код различаются.
# usage: measure_real.sh <таблица.mut> <файл-заголовок в main/> ; 0 = мутант латентен на боевых данных.
set -u
cd "$(dirname "$0")"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
build() { gcc -std=c11 -O2 -I"$1" -I. -o "$2" probe_real.c -lm; }
mkdir "$T/base"; cp ../../main/*.h "$T/base/"; build "$T/base" "$T/base.bin"; "$T/base.bin" > "$T/base.out"
N=$(wc -l < "$T/base.out")
while IFS=$'\t' read -r label expect expr; do
  [ -z "$label" ] && continue
  mkdir "$T/m"; cp ../../main/*.h "$T/m/"; sed -i "$expr" "$T/m/$2"
  if build "$T/m" "$T/m.bin" 2>/dev/null; then "$T/m.bin" > "$T/m.out" 2>/dev/null; D=$(diff "$T/base.out" "$T/m.out" | grep -c '^>'); else D=BUILD_FAIL; fi
  echo "$label: строк_различий=$D из $N"
  rm -rf "$T/m"
done < "$1"
