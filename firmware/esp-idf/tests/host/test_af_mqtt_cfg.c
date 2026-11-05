#include "af_test.h"
#include "af_mqtt_cfg.h"
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdint.h>

static const af_ha_dev_t DEV = {"atomfast_gw", "AtomFast шлюз", "AtomFast Plus2", "0.1.0-e12", "atomfast"};

static void t_default_json(void) {
    af_mqtt_cfg_t c;
    af_mqtt_cfg_default(&c);
    char buf[256];
    size_t n = af_mqtt_cfg_json(&c, buf, sizeof(buf));
    CHECK(n == strlen(buf));
    CHECK(strcmp(buf, "{\"enabled\":0,\"host\":\"\",\"port\":1883,\"user\":\"\",\"pass_set\":0,\"period_s\":60,\"prefix\":\"homeassistant\"}") == 0);
    CHECK(af_mqtt_cfg_valid(&c) == 1);
}

static void t_json_real_no_pass(void) {
    af_mqtt_cfg_t c;
    af_mqtt_cfg_default(&c);
    af_mqtt_cfg_set_enabled(&c, 1);
    af_mqtt_cfg_set_str(&c, "host", "mqtt.home.lan");
    af_mqtt_cfg_set_num(&c, "port", 1883);
    af_mqtt_cfg_set_str(&c, "user", "af_gw");
    af_mqtt_cfg_set_str(&c, "pass", "S3cr3t.Pw-9");
    af_mqtt_cfg_set_num(&c, "period_s", 30);
    af_mqtt_cfg_set_str(&c, "prefix", "homeassistant");

    char buf[256];
    size_t n = af_mqtt_cfg_json(&c, buf, sizeof(buf));
    CHECK(n == strlen(buf));
    CHECK(strcmp(buf, "{\"enabled\":1,\"host\":\"mqtt.home.lan\",\"port\":1883,\"user\":\"af_gw\",\"pass_set\":1,\"period_s\":30,\"prefix\":\"homeassistant\"}") == 0);
    CHECK(strstr(buf, "S3cr3t") == NULL);
    CHECK(strstr(buf, "Pw-9") == NULL);
    CHECK(af_mqtt_cfg_valid(&c) == 1);

    af_mqtt_cfg_set_str(&c, "pass", "");
    n = af_mqtt_cfg_json(&c, buf, sizeof(buf));
    CHECK(strstr(buf, "\"pass_set\":0") != NULL);
}

static void t_json_escape(void) {
    af_mqtt_cfg_t c;
    af_mqtt_cfg_default(&c);
    af_mqtt_cfg_set_str(&c, "user", "a\"b\\c");
    char buf[256];
    size_t n = af_mqtt_cfg_json(&c, buf, sizeof(buf));
    CHECK(n > 0);
    CHECK(strstr(buf, "\"user\":\"a\\\"b\\\\c\"") != NULL);
}

static void t_json_cap(void) {
    af_mqtt_cfg_t c;
    af_mqtt_cfg_default(&c);
    char buf[256];
    size_t n = af_mqtt_cfg_json(&c, buf, sizeof(buf));
    
    char out[256];
    memset(out, 'X', sizeof(out));
    CHECK(af_mqtt_cfg_json(&c, out, n) == 0);
    CHECK(out[0] == '\0');
    
    memset(out, 'X', sizeof(out));
    CHECK(af_mqtt_cfg_json(&c, out, n + 1) == n);
    
    memset(out, 'X', sizeof(out));
    CHECK(af_mqtt_cfg_json(&c, out, 0) == 0);
    CHECK(out[0] == 'X');
    
    CHECK(af_mqtt_cfg_json(NULL, out, sizeof(out)) == 0);
    CHECK(af_mqtt_cfg_json(&c, NULL, sizeof(out)) == 0);
}

static void t_set_num(void) {
    af_mqtt_cfg_t c;
    af_mqtt_cfg_default(&c);
    
    CHECK(af_mqtt_cfg_set_num(&c, "period_s", 9) == -1);
    CHECK(c.period_s == 60);
    CHECK(af_mqtt_cfg_set_num(&c, "period_s", 10) == 0);
    CHECK(c.period_s == 10);
    CHECK(af_mqtt_cfg_set_num(&c, "period_s", 3600) == 0);
    CHECK(c.period_s == 3600);
    CHECK(af_mqtt_cfg_set_num(&c, "period_s", 3601) == -1);
    CHECK(c.period_s == 3600);
    CHECK(af_mqtt_cfg_set_num(&c, "period_s", 60.5) == -1);
    CHECK(c.period_s == 3600);
    CHECK(af_mqtt_cfg_set_num(&c, "period_s", NAN) == -1);
    CHECK(c.period_s == 3600);
    CHECK(af_mqtt_cfg_set_num(&c, "period_s", INFINITY) == -1);
    CHECK(c.period_s == 3600);
    CHECK(af_mqtt_cfg_set_num(&c, "period_s", -1) == -1);
    CHECK(c.period_s == 3600);

    CHECK(af_mqtt_cfg_set_num(&c, "port", 0) == -1);
    CHECK(c.port == 1883);
    CHECK(af_mqtt_cfg_set_num(&c, "port", 1) == 0);
    CHECK(c.port == 1);
    CHECK(af_mqtt_cfg_set_num(&c, "port", 65535) == 0);
    CHECK(c.port == 65535);
    CHECK(af_mqtt_cfg_set_num(&c, "port", 65536) == -1);
    CHECK(c.port == 65535);
    CHECK(af_mqtt_cfg_set_num(&c, "port", 1883.5) == -1);
    CHECK(af_mqtt_cfg_set_num(&c, "port", 1883.0) == 0);
    CHECK(c.port == 1883);

    CHECK(af_mqtt_cfg_set_num(&c, "zzz", 1) == -2);
    CHECK(af_mqtt_cfg_set_num(&c, NULL, 1) == -1);
    CHECK(af_mqtt_cfg_set_num(NULL, "port", 1) == -1);
}

static void t_set_str_host(void) {
    af_mqtt_cfg_t c;
    af_mqtt_cfg_default(&c);
    
    CHECK(af_mqtt_cfg_set_str(&c, "host", "192.168.0.5") == 0);
    CHECK(strcmp(c.host, "192.168.0.5") == 0);
    
    CHECK(af_mqtt_cfg_set_str(&c, "host", "mqtt.home.lan") == 0);
    CHECK(strcmp(c.host, "mqtt.home.lan") == 0);
    
    CHECK(af_mqtt_cfg_set_str(&c, "host", "bad host") == -1);
    CHECK(strcmp(c.host, "mqtt.home.lan") == 0);
    
    CHECK(af_mqtt_cfg_set_str(&c, "host", ".x") == -1);
    CHECK(strcmp(c.host, "mqtt.home.lan") == 0);
    
    CHECK(af_mqtt_cfg_set_str(&c, "host", "-x") == -1);
    CHECK(strcmp(c.host, "mqtt.home.lan") == 0);
    
    CHECK(af_mqtt_cfg_set_str(&c, "host", "a_b-c.d") == 0);
    CHECK(strcmp(c.host, "a_b-c.d") == 0);
    
    char host63[64];
    memset(host63, 'a', 63);
    host63[63] = '\0';
    CHECK(af_mqtt_cfg_set_str(&c, "host", host63) == 0);
    CHECK(strcmp(c.host, host63) == 0);
    
    char host64[65];
    memset(host64, 'a', 64);
    host64[64] = '\0';
    CHECK(af_mqtt_cfg_set_str(&c, "host", host64) == -1);
    CHECK(strcmp(c.host, host63) == 0);
    
    CHECK(af_mqtt_cfg_set_str(&c, "host", "") == 0);
    CHECK(strcmp(c.host, "") == 0);
    
    CHECK(af_mqtt_cfg_set_str(&c, "host", "host\xD1\x8F") == -1);
    CHECK(strcmp(c.host, "") == 0);
}

static void t_set_str_user_pass(void) {
    af_mqtt_cfg_t c;
    af_mqtt_cfg_default(&c);
    
    char user31[32];
    memset(user31, 'u', 31);
    user31[31] = '\0';
    CHECK(af_mqtt_cfg_set_str(&c, "user", user31) == 0);
    CHECK(strcmp(c.user, user31) == 0);
    
    char user32[33];
    memset(user32, 'u', 32);
    user32[32] = '\0';
    CHECK(af_mqtt_cfg_set_str(&c, "user", user32) == -1);
    CHECK(strcmp(c.user, user31) == 0);
    
    char pass63[64];
    memset(pass63, 'p', 63);
    pass63[63] = '\0';
    CHECK(af_mqtt_cfg_set_str(&c, "pass", pass63) == 0);
    CHECK(strcmp(c.pass, pass63) == 0);
    
    char pass64[65];
    memset(pass64, 'p', 64);
    pass64[64] = '\0';
    CHECK(af_mqtt_cfg_set_str(&c, "pass", pass64) == -1);
    CHECK(strcmp(c.pass, pass63) == 0);
    
    CHECK(af_mqtt_cfg_set_str(&c, "pass", "p w") == 0);
    CHECK(strcmp(c.pass, "p w") == 0);
    
    char bad_pass[4] = {'p', 0x07, 'w', '\0'};
    CHECK(af_mqtt_cfg_set_str(&c, "pass", bad_pass) == -1);
    CHECK(strcmp(c.pass, "p w") == 0);
    
    CHECK(af_mqtt_cfg_set_str(&c, "pass", "pw\r\n") == -1);
    CHECK(af_mqtt_cfg_set_str(&c, "pass", "p\tw") == -1);
    CHECK(af_mqtt_cfg_set_str(&c, "user", "u\r\n") == -1);
    CHECK(af_mqtt_cfg_set_str(&c, "user", "u\nx") == -1);
    CHECK(af_mqtt_cfg_set_str(&c, "host", "h\r\n") == -1);
    CHECK(af_mqtt_cfg_set_str(&c, "host", "h\tx") == -1);
    CHECK(strcmp(c.pass, "p w") == 0);

    char bad_user[4] = {'u', 0xD0, 0xA0, '\0'};
    CHECK(af_mqtt_cfg_set_str(&c, "user", bad_user) == -1);
    CHECK(strcmp(c.user, user31) == 0);
    
    CHECK(af_mqtt_cfg_set_str(&c, "user", "") == 0);
    CHECK(strcmp(c.user, "") == 0);
    CHECK(af_mqtt_cfg_set_str(&c, "pass", "") == 0);
    CHECK(strcmp(c.pass, "") == 0);
    
    CHECK(af_mqtt_cfg_set_str(&c, "user", NULL) == -1);
    CHECK(af_mqtt_cfg_set_str(&c, "zzz", "val") == -2);
}

static void t_set_str_prefix(void) {
    af_mqtt_cfg_t c;
    af_mqtt_cfg_default(&c);
    
    CHECK(af_mqtt_cfg_set_str(&c, "prefix", "homeassistant") == 0);
    CHECK(strcmp(c.prefix, "homeassistant") == 0);
    
    CHECK(af_mqtt_cfg_set_str(&c, "prefix", "ha/x-1") == 0);
    CHECK(strcmp(c.prefix, "ha/x-1") == 0);
    
    CHECK(af_mqtt_cfg_set_str(&c, "prefix", "Home") == -1);
    CHECK(strcmp(c.prefix, "ha/x-1") == 0);
    
    CHECK(af_mqtt_cfg_set_str(&c, "prefix", "") == -1);
    CHECK(strcmp(c.prefix, "ha/x-1") == 0);
    
    CHECK(af_mqtt_cfg_set_str(&c, "prefix", "/a") == -1);
    CHECK(strcmp(c.prefix, "ha/x-1") == 0);
    
    CHECK(af_mqtt_cfg_set_str(&c, "prefix", "a/") == -1);
    CHECK(strcmp(c.prefix, "ha/x-1") == 0);
    
    char pref32[33];
    memset(pref32, 'a', 32);
    pref32[32] = '\0';
    CHECK(af_mqtt_cfg_set_str(&c, "prefix", pref32) == 0);
    CHECK(strcmp(c.prefix, pref32) == 0);
    
    char pref33[34];
    memset(pref33, 'a', 33);
    pref33[33] = '\0';
    CHECK(af_mqtt_cfg_set_str(&c, "prefix", pref33) == -1);
    CHECK(strcmp(c.prefix, pref32) == 0);
}

static void t_set_enabled(void) {
    af_mqtt_cfg_t c;
    af_mqtt_cfg_default(&c);
    
    CHECK(af_mqtt_cfg_set_enabled(&c, 0) == 0);
    CHECK(c.enabled == 0);
    
    CHECK(af_mqtt_cfg_set_enabled(&c, 1) == 0);
    CHECK(c.enabled == 1);
    
    CHECK(af_mqtt_cfg_set_enabled(&c, 2) == -1);
    CHECK(c.enabled == 1);
    
    CHECK(af_mqtt_cfg_set_enabled(&c, -1) == -1);
    CHECK(c.enabled == 1);
    
    CHECK(af_mqtt_cfg_set_enabled(NULL, 1) == -1);
}

static void t_valid(void) {
    af_mqtt_cfg_t c;
    af_mqtt_cfg_default(&c);
    CHECK(af_mqtt_cfg_valid(&c) == 1);
    
    c.enabled = 1;
    c.host[0] = '\0';
    CHECK(af_mqtt_cfg_valid(&c) == 0);
    
    strcpy(c.host, "mqtt.home.lan");
    CHECK(af_mqtt_cfg_valid(&c) == 1);
    
    c.port = 0;
    CHECK(af_mqtt_cfg_valid(&c) == 0);
    c.port = 1883;
    
    c.period_s = 9;
    CHECK(af_mqtt_cfg_valid(&c) == 0);
    c.period_s = 60;
    
    c.period_s = 3601;
    CHECK(af_mqtt_cfg_valid(&c) == 0);
    c.period_s = 60;
    
    c.prefix[0] = '\0';
    CHECK(af_mqtt_cfg_valid(&c) == 0);
    strcpy(c.prefix, "homeassistant");
    
    strcpy(c.prefix, "Home");
    CHECK(af_mqtt_cfg_valid(&c) == 0);
    strcpy(c.prefix, "homeassistant");
    
    memset(c.host, 'a', sizeof(c.host));
    CHECK(af_mqtt_cfg_valid(&c) == 0);
    strcpy(c.host, "-x");
    CHECK(af_mqtt_cfg_valid(&c) == 0);
    strcpy(c.host, "mqtt.home.lan");

    c.user[0] = 0x01;
    CHECK(af_mqtt_cfg_valid(&c) == 0);
    c.user[0] = '\0';

    memset(c.user, 'u', sizeof(c.user));
    CHECK(af_mqtt_cfg_valid(&c) == 0);
    c.user[0] = '\0';
    
    memset(c.pass, 'p', sizeof(c.pass));
    CHECK(af_mqtt_cfg_valid(&c) == 0);
    c.pass[0] = '\0';
    
    c.enabled = 2;
    CHECK(af_mqtt_cfg_valid(&c) == 0);
    c.enabled = 1;
    
    CHECK(af_mqtt_cfg_valid(NULL) == 0);
}

static void t_stat_json(void) {
    af_mqtt_stat_t st = {1, 1, 0, 123, 4};
    char buf[256];
    size_t n = af_mqtt_stat_json(&st, buf, sizeof(buf));
    CHECK(n == strlen(buf));
    CHECK(strcmp(buf, "{\"enabled\":1,\"connected\":1,\"last_err\":\"\",\"published\":123,\"failed\":4}") == 0);
    
    st = (af_mqtt_stat_t){0, 0, AF_MQTT_E_NOHOST, 0, 0};
    n = af_mqtt_stat_json(&st, buf, sizeof(buf));
    CHECK(strcmp(buf, "{\"enabled\":0,\"connected\":0,\"last_err\":\"no_host\",\"published\":0,\"failed\":0}") == 0);
    
    const char* names[] = {"", "no_host", "transport", "auth", "publish", "init"};
    for (int i = 0; i < AF_MQTT_E_COUNT; i++) {
        st.last_err = (uint8_t)i;
        n = af_mqtt_stat_json(&st, buf, sizeof(buf));
        char expected[64];
        snprintf(expected, sizeof(expected), "\"last_err\":\"%s\"", names[i]);
        CHECK(strstr(buf, expected) != NULL);
    }
    
    st.last_err = 99;
    n = af_mqtt_stat_json(&st, buf, sizeof(buf));
    CHECK(strstr(buf, "\"last_err\":\"unknown\"") != NULL);
    
    st.published = 4294967295u;
    n = af_mqtt_stat_json(&st, buf, sizeof(buf));
    CHECK(strstr(buf, "\"published\":4294967295") != NULL);
    
    n = af_mqtt_stat_json(&st, buf, sizeof(buf));
    CHECK(af_mqtt_stat_json(&st, buf, n) == 0);
    CHECK(af_mqtt_stat_json(NULL, buf, sizeof(buf)) == 0);
}

static void t_config_topic_prefix(void) {
    char buf[256];
    char ref[256];
    
    for (int i = 0; i < AF_HA_SENSOR_COUNT; i++) {
        size_t n1 = af_mqtt_config_topic_p("homeassistant", &DEV, &AF_HA_SENSORS[i], buf, sizeof(buf));
        size_t n2 = af_ha_config_topic(&DEV, &AF_HA_SENSORS[i], ref, sizeof(ref));
        CHECK(n1 == n2);
        CHECK(strcmp(buf, ref) == 0);
    }
    
    af_mqtt_config_topic_p("homeassistant", &DEV, &AF_HA_SENSORS[0], buf, sizeof(buf));
    CHECK(strcmp(buf, "homeassistant/sensor/atomfast_gw/dose_rate/config") == 0);
    
    af_mqtt_config_topic_p("ha/x-1", &DEV, &AF_HA_SENSORS[0], buf, sizeof(buf));
    CHECK(strcmp(buf, "ha/x-1/sensor/atomfast_gw/dose_rate/config") == 0);
    
    CHECK(af_mqtt_config_topic_p("Home", &DEV, &AF_HA_SENSORS[0], buf, sizeof(buf)) == 0);
    CHECK(buf[0] == '\0');
    
    CHECK(af_mqtt_config_topic_p(NULL, &DEV, &AF_HA_SENSORS[0], buf, sizeof(buf)) == 0);
    
    size_t len = af_mqtt_config_topic_p("homeassistant", &DEV, &AF_HA_SENSORS[0], buf, sizeof(buf));
    CHECK(af_mqtt_config_topic_p("homeassistant", &DEV, &AF_HA_SENSORS[0], buf, len) == 0);
    CHECK(af_mqtt_config_topic_p("homeassistant", &DEV, &AF_HA_SENSORS[0], buf, len + 1) == len);
}

static void t_ha_status_topic(void) {
    char buf[256];
    
    CHECK(strcmp(af_mqtt_ha_status_topic("homeassistant", buf, sizeof(buf)) ? buf : "", "homeassistant/status") == 0);
    CHECK(strcmp(af_mqtt_ha_status_topic("ha/x-1", buf, sizeof(buf)) ? buf : "", "ha/x-1/status") == 0);
    CHECK(af_mqtt_ha_status_topic("", buf, sizeof(buf)) == 0);
    CHECK(af_mqtt_ha_status_topic("Home", buf, sizeof(buf)) == 0);
    CHECK(af_mqtt_ha_status_topic("homeassistant", buf, 10) == 0);
}

static void t_ble_topics_json(void) {
    char buf[256];
    
    CHECK(strcmp(af_mqtt_ble_topic(&DEV, buf, sizeof(buf)) ? buf : "", "atomfast/atomfast_gw/ble") == 0);
    CHECK(strcmp(af_mqtt_ble_config_topic("homeassistant", &DEV, buf, sizeof(buf)) ? buf : "", "homeassistant/binary_sensor/atomfast_gw/ble/config") == 0);
    
    CHECK(af_mqtt_ble_config_topic("Home", &DEV, buf, sizeof(buf)) == 0);
    const char* expected_json = "{\"name\":\"BLE AtomFast\",\"uniq_id\":\"atomfast_gw_ble\",\"stat_t\":\"atomfast/atomfast_gw/ble\",\"avty_t\":\"atomfast/atomfast_gw/status\",\"pl_on\":\"ON\",\"pl_off\":\"OFF\",\"dev_cla\":\"connectivity\",\"dev\":{\"ids\":[\"atomfast_gw\"]}}";
    size_t n = af_mqtt_ble_config_json(&DEV, buf, sizeof(buf));
    CHECK(n == strlen(buf));
    CHECK(strcmp(buf, expected_json) == 0);
    
    af_ha_dev_t bad_dev = DEV;
    bad_dev.node_id = "Bad Id";
    CHECK(af_mqtt_ble_topic(&bad_dev, buf, sizeof(buf)) == 0);
    CHECK(af_mqtt_ble_config_topic("homeassistant", &bad_dev, buf, sizeof(buf)) == 0);
    CHECK(af_mqtt_ble_config_json(&bad_dev, buf, sizeof(buf)) == 0);
    
    n = af_mqtt_ble_config_json(&DEV, buf, sizeof(buf));
    CHECK(af_mqtt_ble_config_json(&DEV, buf, n) == 0);
}

static void t_air_caps(void) {
    CHECK(af_mqtt_air_caps(0x03, 26.16f, 758.54f) == 0x03);
    CHECK(af_mqtt_air_caps(0x01, 26.16f, 758.54f) == 0x01);
    CHECK(af_mqtt_air_caps(0x02, 26.16f, 758.54f) == 0x02);
    CHECK(af_mqtt_air_caps(0x03, NAN, 758.54f) == 0x02);
    CHECK(af_mqtt_air_caps(0x03, 26.16f, NAN) == 0x01);
    CHECK(af_mqtt_air_caps(0x03, NAN, NAN) == 0);
    CHECK(af_mqtt_air_caps(0x00, 26.16f, 758.54f) == 0);
    CHECK(af_mqtt_air_caps(0x04, 26.16f, 758.54f) == 0);
}

static void t_due(void) {
    CHECK(af_mqtt_due(100, 0, 60, 0) == 1);
    CHECK(af_mqtt_due(100, 100, 60, 0) == 1);
    CHECK(af_mqtt_due(100, 100, 60, 1) == 0);
    CHECK(af_mqtt_due(159, 100, 60, 1) == 0);
    CHECK(af_mqtt_due(160, 100, 60, 1) == 1);
    CHECK(af_mqtt_due(5, 4294967290u, 60, 1) == 1);
    CHECK(af_mqtt_due(4294967295u, 4294967290u, 60, 1) == 0);
    CHECK(af_mqtt_due(200, 100, 3600, 1) == 0);
}

static void t_match(void) {
    CHECK(af_mqtt_match("online", 6, "online") == 1);
    CHECK(af_mqtt_match("online", 5, "online") == 0);
    CHECK(af_mqtt_match("offline", 6, "online") == 0);
    CHECK(af_mqtt_match("onlineX", 6, "online") == 1);
    CHECK(af_mqtt_match("online", -1, "online") == 0);
    CHECK(af_mqtt_match(NULL, 6, "online") == 0);
    CHECK(af_mqtt_match("online", 6, NULL) == 0);
    CHECK(af_mqtt_match("homeassistant/status", 20, "homeassistant/status") == 1);
}

static void t_state_real(void) {
    af_ha_state_t st = {
        .dose_rate = 0.0825404077768326f,
        .cps = 24.5f,
        .dose = 0.19282141327857971f * 1000.0f,
        .pulses_2s = 49,
        .battery = 27,
        .temp_af = 25,
        .air_temp = 26.16f,
        .air_press = 758.54f,
        .ts = 1792873394u
    };
    
    char buf[768];
    size_t n = af_ha_state_json(&st, 3, buf, sizeof(buf));
    CHECK(n == strlen(buf));
    CHECK(strcmp(buf, "{\"ts\":1792873394,\"dose_rate\":0.0825,\"dose_rate_ur\":8.25,\"cps\":24.50,\"pulses_2s\":49,\"dose\":192.821,\"battery\":27,\"temp_af\":25,\"air_temp\":26.2,\"air_press\":758.5}") == 0);
    
    n = af_ha_state_json(&st, 1, buf, sizeof(buf));
    CHECK(strcmp(buf, "{\"ts\":1792873394,\"dose_rate\":0.0825,\"dose_rate_ur\":8.25,\"cps\":24.50,\"pulses_2s\":49,\"dose\":192.821,\"battery\":27,\"temp_af\":25,\"air_temp\":26.2}") == 0);
    
    n = af_ha_state_json(&st, 0, buf, sizeof(buf));
    CHECK(strcmp(buf, "{\"ts\":1792873394,\"dose_rate\":0.0825,\"dose_rate_ur\":8.25,\"cps\":24.50,\"pulses_2s\":49,\"dose\":192.821,\"battery\":27,\"temp_af\":25}") == 0);
    
    st.air_press = NAN;
    n = af_ha_state_json(&st, 3, buf, sizeof(buf));
    CHECK(strcmp(buf, "{\"ts\":1792873394,\"dose_rate\":0.0825,\"dose_rate_ur\":8.25,\"cps\":24.50,\"pulses_2s\":49,\"dose\":192.821,\"battery\":27,\"temp_af\":25,\"air_temp\":26.2}") == 0);
    
    st.dose_rate = NAN;
    CHECK(af_ha_state_json(&st, 3, buf, sizeof(buf)) == 0);
    
    st.dose_rate = 0.0825404077768326f;
    n = af_ha_state_json(&st, 3, buf, sizeof(buf));
    CHECK(n < 768);
}

static void t_discovery_fits_driver_buf(void) {
    char buf[768];
    size_t maxlen = 0;
    
    for (int i = 0; i < AF_HA_SENSOR_COUNT; i++) {
        size_t n = af_ha_config_json(&DEV, &AF_HA_SENSORS[i], buf, sizeof(buf));
        CHECK(n > 0);
        CHECK(n < 768);
        CHECK(buf[n-1] == '}');
        
        char expected_uniq[64];
        snprintf(expected_uniq, sizeof(expected_uniq), "\"uniq_id\":\"atomfast_gw_%s\"", AF_HA_SENSORS[i].key);
        CHECK(strstr(buf, expected_uniq) != NULL);
        
        if (n > maxlen) maxlen = n;
    }
    
    printf("INFO max_discovery_len=%zu\n", maxlen);
    
    af_ha_state_t st = {
        .ts = 4294967295u,
        .dose_rate = 999.0f,
        .cps = 99999.0f,
        .dose = 99999999.0f,
        .pulses_2s = 65535,
        .battery = -128,
        .temp_af = -128,
        .air_temp = -40.0f,
        .air_press = 999.9f
    };
    
    size_t n = af_ha_state_json(&st, 3, buf, sizeof(buf));
    CHECK(n > 0);
    CHECK(n < 768);
}

static void t_pub_allowed(void)
{
    char buf[256];
    size_t len;

    for (int i = 0; i < AF_HA_SENSOR_COUNT; i++) {
        len = af_mqtt_config_topic_p("homeassistant", &DEV, &AF_HA_SENSORS[i], buf, sizeof(buf));
        CHECK(len > 0);
        CHECK(af_mqtt_pub_allowed("homeassistant", buf, 1) == 1);
        CHECK(af_mqtt_pub_allowed("homeassistant", buf, 0) == 1);
    }

    len = af_mqtt_ble_config_topic("homeassistant", &DEV, buf, sizeof(buf));
    CHECK(len > 0);
    CHECK(af_mqtt_pub_allowed("homeassistant", buf, 1) == 1);

    len = af_ha_avail_topic(&DEV, buf, sizeof(buf));
    CHECK(len > 0);
    CHECK(af_mqtt_pub_allowed("homeassistant", buf, 1) == 1);
    CHECK(af_mqtt_pub_allowed("homeassistant", buf, 0) == 1);

    len = af_ha_state_topic(&DEV, buf, sizeof(buf));
    CHECK(len > 0);
    CHECK(af_mqtt_pub_allowed("homeassistant", buf, 0) == 1);
    CHECK(af_mqtt_pub_allowed("homeassistant", buf, 1) == 0);

    len = af_mqtt_ble_topic(&DEV, buf, sizeof(buf));
    CHECK(len > 0);
    CHECK(af_mqtt_pub_allowed("homeassistant", buf, 0) == 1);
    CHECK(af_mqtt_pub_allowed("homeassistant", buf, 1) == 0);

    const char *foreign_topics[] = {
        "homeassistant/sensor/other_dev/x/config",
        "homeassistant/switch/atomfast_gw/x/config",
        "zigbee2mqtt/atomfast_gw/state",
        "atomfast/other/state",
        "homeassistant/sensor/atomfast_gw/#",
        "atomfast/atomfast_gw/+/state",
        "homeassistant/sensor/atomfast_",
        "atomfast/atomfast_",
        "homeassistant/status",
        "$SYS/broker/version",
        "",
        "homeassistant//sensor/atomfast_gw/x/config"
    };
    for (size_t i = 0; i < sizeof(foreign_topics) / sizeof(foreign_topics[0]); i++) {
        CHECK(af_mqtt_pub_allowed("homeassistant", foreign_topics[i], 0) == 0);
        CHECK(af_mqtt_pub_allowed("homeassistant", foreign_topics[i], 1) == 0);
    }

    CHECK(af_mqtt_pub_allowed("homeassistant", NULL, 0) == 0);
    CHECK(af_mqtt_pub_allowed(NULL, "homeassistant/sensor/atomfast_gw/x/config", 0) == 0);
    CHECK(af_mqtt_pub_allowed("Bad", "Bad/sensor/atomfast_gw/x/config", 0) == 0);
    CHECK(af_mqtt_pub_allowed("", "/sensor/atomfast_gw/x/config", 0) == 0);

    CHECK(af_mqtt_pub_allowed("ha/x-1", "ha/x-1/sensor/atomfast_gw/dose_rate/config", 1) == 1);
    CHECK(af_mqtt_pub_allowed("ha/x-1", "homeassistant/sensor/atomfast_gw/dose_rate/config", 1) == 0);

    CHECK(af_mqtt_pub_allowed("homeassistant", "homeassistant/sensor/atomfast_gw/dose_rate/state", 1) == 0);
    CHECK(af_mqtt_pub_allowed("homeassistant", "homeassistant/binary_sensor/atomfast_gw/ble/state", 1) == 0);
    CHECK(af_mqtt_pub_allowed("homeassistant", "atomfast/atomfast_gw//state", 0) == 0);
    char long_topic[130];
    memset(long_topic, 'a', 128);
    memcpy(long_topic, "atomfast/atomfast_", 18);
    long_topic[128] = 0;
    CHECK(af_mqtt_pub_allowed("homeassistant", long_topic, 0) == 0);

    long_topic[127] = 0;
    CHECK(af_mqtt_pub_allowed("homeassistant", long_topic, 0) == 1);
}

static void t_sub_allowed(void)
{
    CHECK(af_mqtt_sub_allowed("homeassistant", "homeassistant/status") == 1);
    CHECK(af_mqtt_sub_allowed("homeassistant", "homeassistant/#") == 0);
    CHECK(af_mqtt_sub_allowed("homeassistant", "homeassistant/status/x") == 0);
    CHECK(af_mqtt_sub_allowed("homeassistant", "atomfast/#") == 0);
    CHECK(af_mqtt_sub_allowed("homeassistant", "#") == 0);
    CHECK(af_mqtt_sub_allowed("ha/x-1", "ha/x-1/status") == 1);
    CHECK(af_mqtt_sub_allowed("ha/x-1", "homeassistant/status") == 0);
    CHECK(af_mqtt_sub_allowed(NULL, "x") == 0);
    CHECK(af_mqtt_sub_allowed("homeassistant", NULL) == 0);
    CHECK(af_mqtt_sub_allowed("Bad", "Bad/status") == 0);
}

/* e14: жизненный цикл задачи MQTT (создаётся только при enabled, завершается при выключении) */
static void t_task_action(void) {
    /* вне задачи (старт, POST настроек): создать, только если включено и не запущена */
    CHECK(af_mqtt_task_action(1, 0, 0) == AF_MQTT_ACT_SPAWN);
    CHECK(af_mqtt_task_action(1, 1, 0) == AF_MQTT_ACT_NONE);
    CHECK(af_mqtt_task_action(0, 0, 0) == AF_MQTT_ACT_NONE);
    CHECK(af_mqtt_task_action(0, 1, 0) == AF_MQTT_ACT_NONE);
    /* внутри задачи: выключено - выйти (освободить клиента и буферы), включено - работать */
    CHECK(af_mqtt_task_action(0, 1, 1) == AF_MQTT_ACT_EXIT);
    CHECK(af_mqtt_task_action(1, 1, 1) == AF_MQTT_ACT_NONE);
    CHECK(af_mqtt_task_action(0, 0, 1) == AF_MQTT_ACT_EXIT);
    /* значения enabled кроме 0/1 трактуются как включено */
    CHECK(af_mqtt_task_action(2, 0, 0) == AF_MQTT_ACT_SPAWN);
    CHECK(af_mqtt_task_action(2, 1, 1) == AF_MQTT_ACT_NONE);
    /* последовательность: включили, задача жива, выключили, задача вышла, включили снова */
    int running = 0, a = af_mqtt_task_action(1, running, 0);
    CHECK(a == AF_MQTT_ACT_SPAWN); running = 1;
    CHECK(af_mqtt_task_action(1, running, 0) == AF_MQTT_ACT_NONE);
    CHECK(af_mqtt_task_action(0, running, 1) == AF_MQTT_ACT_EXIT); running = 0;
    CHECK(af_mqtt_task_action(1, running, 0) == AF_MQTT_ACT_SPAWN);
}
int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);  /* при аварийном останове (fortify/ASan) уже напечатанное не теряется */
    RUN(t_default_json);
    RUN(t_json_real_no_pass);
    RUN(t_json_escape);
    RUN(t_json_cap);
    RUN(t_set_num);
    RUN(t_set_str_host);
    RUN(t_set_str_user_pass);
    RUN(t_set_str_prefix);
    RUN(t_set_enabled);
    RUN(t_valid);
    RUN(t_stat_json);
    RUN(t_config_topic_prefix);
    RUN(t_ha_status_topic);
    RUN(t_ble_topics_json);
    RUN(t_air_caps);
    RUN(t_due);
    RUN(t_match);
    RUN(t_state_real);
    RUN(t_discovery_fits_driver_buf);
    RUN(t_pub_allowed);
    RUN(t_sub_allowed);
    RUN(t_task_action);
    TESTS_DONE();
}
