cd /project/tests/host
echo "### objcheck"
for f in af_log.c af_log_http.c af_persist_fs.c af_store.c ble_gw.c main.c af_mqtt_drv.c af_env_drv.c web_server.c ota_github_client.c; do python3 objcheck.py $f >/tmp/oc.log 2>&1; r=$?; echo "$f rc=$r"; [ $r -ne 0 ] && tail -3 /tmp/oc.log; done
echo "### make test"; make clean >/dev/null; AF_DUMP_DIR=/dump/d1 make test 2>&1 | grep -E "^TESTS|PASSED|FAIL|rror" | sort | uniq -c | head -40
make clean >/dev/null
echo "### make asan"; AF_DUMP_DIR=/dump/d1 make asan 2>&1 | grep -E "^TESTS|FAIL|ERROR|runtime error" | sort | uniq -c | head -40
make clean >/dev/null
echo FINISHED
