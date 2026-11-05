#!/usr/bin/env bash
# Компиляция ОДНОГО файла main/*.c флагами xtensa-gcc из build-e1/compile_commands.json (образ не собирается). usage: syntax_idf.sh main/af_push.c [доп.флаги]
# Запускать из каталога проекта (/project в контейнере espressif/idf:v5.4). Печатает ошибки/предупреждения, затем размеры кадров (-fstack-usage).
set -u
F=$1; shift
cd /project || exit 9
CMD=$(python3 - "$F" <<'PY'
import json, sys, re, shlex
d = json.load(open('build-e1/compile_commands.json'))
ref = [e for e in d if e['file'].endswith('main/af_mqtt_drv.c')][0]
c = ref['command']
c = re.sub(r' -DAF_AUTH_DEFAULT_PASS=\S+', '', c)
c = re.sub(r' -o \S+', '', c)
c = re.sub(r' -c \S+$', '', c)
c = re.sub(r' -MD -MT \S+ -MF \S+', '', c)
print(c)
PY
)
mkdir -p /tmp/sx
eval "$CMD" "$@" -fstack-usage -c "$F" -o /tmp/sx/out.o
rc=$?
echo "COMPILE_RC=$rc"
[ -f /tmp/sx/out.su ] && sort -t$'\t' -k2 -n -r /tmp/sx/out.su | head -12
