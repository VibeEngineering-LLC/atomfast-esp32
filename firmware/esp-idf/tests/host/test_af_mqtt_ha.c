#include "af_mqtt_ha.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static int failures = 0;
static int tests = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("  FAIL %s:%d: %s\n", __func__, __LINE__, #cond); \
        failures++; \
    } \
} while(0)

static const af_ha_dev_t DEV = {"atomfast_gw", "AtomFast шлюз", "AtomFast Plus2", "0.1.0-e10", "atomfast"};

static const af_ha_sensor_t *find_sensor(const char *key) {
    for (size_t i = 0; i < AF_HA_SENSOR_COUNT; i++) {
        if (strcmp(AF_HA_SENSORS[i].key, key) == 0) {
            return &AF_HA_SENSORS[i];
        }
    }
    return NULL;
}

static void t_valid_id(void) { int f0 = failures;
    tests++;
    CHECK(af_ha_valid_id("atomfast_gw", 0) == 1);
    CHECK(af_ha_valid_id("", 0) == 0);
    
    char long_id[34];
    memset(long_id, 'a', 33);
    long_id[33] = '\0';
    CHECK(af_ha_valid_id(long_id, 0) == 0);
    
    CHECK(af_ha_valid_id("Atom", 0) == 0);
    CHECK(af_ha_valid_id("a-b", 0) == 0);
    CHECK(af_ha_valid_id("a-b", 1) == 1);
    CHECK(af_ha_valid_id("a/b", 1) == 1);
    CHECK(af_ha_valid_id("/a", 1) == 0);
    CHECK(af_ha_valid_id("a/", 1) == 0);
    
    if (failures == f0) printf("PASS t_valid_id\n");
    else printf("FAIL t_valid_id\n");
}

static void t_topics(void) { int f0 = failures;
    tests++;
    char b[1024];
    
    size_t len = af_ha_state_topic(&DEV, b, sizeof(b));
    CHECK(len == strlen("atomfast/atomfast_gw/state"));
    CHECK(strcmp(b, "atomfast/atomfast_gw/state") == 0);
    
    len = af_ha_avail_topic(&DEV, b, sizeof(b));
    CHECK(len == strlen("atomfast/atomfast_gw/status"));
    CHECK(strcmp(b, "atomfast/atomfast_gw/status") == 0);
    
    const af_ha_sensor_t *s = &AF_HA_SENSORS[0];
    len = af_ha_config_topic(&DEV, s, b, sizeof(b));
    CHECK(len == strlen("homeassistant/sensor/atomfast_gw/dose_rate/config"));
    CHECK(strcmp(b, "homeassistant/sensor/atomfast_gw/dose_rate/config") == 0);
    
    if (failures == f0) printf("PASS t_topics\n");
    else printf("FAIL t_topics\n");
}

static void t_topic_small_buf(void) { int f0 = failures;
    tests++;
    char b[1024];
    const char *expected = "atomfast/atomfast_gw/state";
    size_t exp_len = strlen(expected);
    
    // cap = exp_len (not enough for null terminator)
    size_t len = af_ha_state_topic(&DEV, b, exp_len);
    CHECK(len == 0);
    CHECK(b[0] == '\0');
    
    // cap = exp_len + 1 (just enough)
    len = af_ha_state_topic(&DEV, b, exp_len + 1);
    CHECK(len == exp_len);
    CHECK(strcmp(b, expected) == 0);
    
    if (failures == f0) printf("PASS t_topic_small_buf\n");
    else printf("FAIL t_topic_small_buf\n");
}

static void t_bad_dev(void) { int f0 = failures;
    tests++;
    char b[1024];
    
    af_ha_dev_t bad_id = {"Bad Id", "x", "x", "x", "atomfast"};
    size_t len = af_ha_state_topic(&bad_id, b, sizeof(b));
    CHECK(len == 0);
    
    af_ha_dev_t bad_base = {"atomfast_gw", "x", "x", "x", "/atomfast"};
    len = af_ha_state_topic(&bad_base, b, sizeof(b));
    CHECK(len == 0);
    
    if (failures == f0) printf("PASS t_bad_dev\n");
    else printf("FAIL t_bad_dev\n");
}

static void t_escape(void) { int f0 = failures;
    tests++;
    char b[1024];
    
    // Quote and backslash
    const char *in1 = "a\"b\\c";
    size_t len = af_json_escape(in1, b, sizeof(b));
    CHECK(len == strlen("a\\\"b\\\\c"));
    CHECK(strcmp(b, "a\\\"b\\\\c") == 0);
    
    // Control char 0x01
    const char *in2 = "\x01";
    len = af_json_escape(in2, b, sizeof(b));
    CHECK(len == strlen("\\u0001"));
    CHECK(strcmp(b, "\\u0001") == 0);
    
    // UTF-8 passthrough
    const char *in3 = "\xC2\xB5Sv"; // µSv
    len = af_json_escape(in3, b, sizeof(b));
    CHECK(len == strlen(in3));
    CHECK(strcmp(b, in3) == 0);
    
    // Buffer too small
    len = af_json_escape(in1, b, 2);
    CHECK(len == 0);
    
    if (failures == f0) printf("PASS t_escape\n");
    else printf("FAIL t_escape\n");
}

static void t_config_dose_rate(void) { int f0 = failures;
    tests++;
    char b[1024];
    const af_ha_sensor_t *s = find_sensor("dose_rate");
    CHECK(s != NULL);
    if (!s) {
        printf("FAIL t_config_dose_rate\n");
        return;
    }
    
    size_t len = af_ha_config_json(&DEV, s, b, sizeof(b));
    const char *expected = "{\"name\":\"Мощность дозы\",\"uniq_id\":\"atomfast_gw_dose_rate\",\"stat_t\":\"atomfast/atomfast_gw/state\",\"avty_t\":\"atomfast/atomfast_gw/status\",\"val_tpl\":\"{{ value_json.dose_rate }}\",\"unit_of_meas\":\"µSv/h\",\"stat_cla\":\"measurement\",\"sug_dsp_prc\":4,\"dev\":{\"ids\":[\"atomfast_gw\"],\"name\":\"AtomFast шлюз\",\"mdl\":\"AtomFast Plus2\",\"mf\":\"VibeEngineering\",\"sw\":\"0.1.0-e10\"}}";
    
    CHECK(len == strlen(expected));
    CHECK(strcmp(b, expected) == 0);
    
    if (failures == f0) printf("PASS t_config_dose_rate\n");
    else printf("FAIL t_config_dose_rate\n");
}

static void t_config_optional_fields(void) { int f0 = failures;
    tests++;
    char b[1024];
    
    // pulses_2s: no unit_of_meas, no dev_cla
    const af_ha_sensor_t *s_pulses = find_sensor("pulses_2s");
    CHECK(s_pulses != NULL);
    if (s_pulses) {
        size_t len = af_ha_config_json(&DEV, s_pulses, b, sizeof(b));
        CHECK(len > 0);
        CHECK(strstr(b, "\"unit_of_meas\"") == NULL);
        CHECK(strstr(b, "\"dev_cla\"") == NULL);
    }
    
    // air_press: has unit_of_meas mmHg, dev_cla pressure
    const af_ha_sensor_t *s_press = find_sensor("air_press");
    CHECK(s_press != NULL);
    if (s_press) {
        size_t len = af_ha_config_json(&DEV, s_press, b, sizeof(b));
        CHECK(len > 0);
        CHECK(strstr(b, "\"unit_of_meas\":\"mmHg\"") != NULL);
        CHECK(strstr(b, "\"dev_cla\":\"pressure\"") != NULL);
    }
    
    // battery: has dev_cla battery
    const af_ha_sensor_t *s_batt = find_sensor("battery");
    CHECK(s_batt != NULL);
    if (s_batt) {
        size_t len = af_ha_config_json(&DEV, s_batt, b, sizeof(b));
        CHECK(len > 0);
        CHECK(strstr(b, "\"dev_cla\":\"battery\"") != NULL);
    }
    
    if (failures == f0) printf("PASS t_config_optional_fields\n");
    else printf("FAIL t_config_optional_fields\n");
}

static void t_sensor_table(void) { int f0 = failures;
    tests++;
    CHECK(AF_HA_SENSOR_COUNT == 9);
    
    // Check unique keys
    for (size_t i = 0; i < AF_HA_SENSOR_COUNT; i++) {
        for (size_t j = i + 1; j < AF_HA_SENSOR_COUNT; j++) {
            CHECK(strcmp(AF_HA_SENSORS[i].key, AF_HA_SENSORS[j].key) != 0);
        }
    }
    
    // Check dose state_class
    const af_ha_sensor_t *s_dose = find_sensor("dose");
    CHECK(s_dose != NULL);
    if (s_dose) {
        CHECK(strcmp(s_dose->state_class, "total_increasing") == 0);
    }
    
    if (failures == f0) printf("PASS t_sensor_table\n");
    else printf("FAIL t_sensor_table\n");
}

static void t_state_full(void) { int f0 = failures;
    tests++;
    char b[1024];
    
    af_ha_state_t st = {
        .dose_rate = 0.0750f,
        .cps = 15.50f,
        .dose = 174.810f,
        .pulses_2s = 31,
        .battery = 27,
        .temp_af = 26,
        .air_temp = 26.5f,
        .air_press = 751.7f,
        .ts = 1792861680u
    };
    
    size_t len = af_ha_state_json(&st, 0x03, b, sizeof(b));
    const char *expected = "{\"ts\":1792861680,\"dose_rate\":0.0750,\"dose_rate_ur\":7.50,\"cps\":15.50,\"pulses_2s\":31,\"dose\":174.810,\"battery\":27,\"temp_af\":26,\"air_temp\":26.5,\"air_press\":751.7}";
    
    CHECK(len == strlen(expected));
    CHECK(strcmp(b, expected) == 0);
    
    if (failures == f0) printf("PASS t_state_full\n");
    else printf("FAIL t_state_full\n");
}

static void t_state_caps(void) { int f0 = failures;
    tests++;
    char b[1024];
    
    af_ha_state_t st = {
        .dose_rate = 0.0750f,
        .cps = 15.50f,
        .dose = 174.810f,
        .pulses_2s = 31,
        .battery = 27,
        .temp_af = 26,
        .air_temp = 26.5f,
        .air_press = 751.7f,
        .ts = 1792861680u
    };
    
    // caps 0x01: no air_press
    size_t len = af_ha_state_json(&st, 0x01, b, sizeof(b));
    CHECK(len > 0);
    CHECK(strstr(b, "\"air_press\"") == NULL);
    
    // caps 0x00: no air_temp, no air_press
    len = af_ha_state_json(&st, 0x00, b, sizeof(b));
    CHECK(len > 0);
    CHECK(strstr(b, "\"air_temp\"") == NULL);
    CHECK(strstr(b, "\"air_press\"") == NULL);
    
    // air_press NAN with caps 0x03: no air_press, has air_temp
    st.air_press = NAN;
    len = af_ha_state_json(&st, 0x03, b, sizeof(b));
    CHECK(len > 0);
    CHECK(strstr(b, "\"air_press\"") == NULL);
    CHECK(strstr(b, "\"air_temp\"") != NULL);
    
    if (failures == f0) printf("PASS t_state_caps\n");
    else printf("FAIL t_state_caps\n");
}

static void t_state_invalid(void) { int f0 = failures;
    tests++;
    char b[1024];
    
    af_ha_state_t st = {
        .dose_rate = 0.0750f,
        .cps = 15.50f,
        .dose = 174.810f,
        .pulses_2s = 31,
        .battery = 27,
        .temp_af = 26,
        .air_temp = 26.5f,
        .air_press = 751.7f,
        .ts = 1792861680u
    };
    
    // dose_rate NAN
    st.dose_rate = NAN;
    size_t len = af_ha_state_json(&st, 0x03, b, sizeof(b));
    CHECK(len == 0);
    
    // cps INFINITY
    st.dose_rate = 0.0750f;
    st.cps = INFINITY;
    len = af_ha_state_json(&st, 0x03, b, sizeof(b));
    CHECK(len == 0);
    
    // dose NAN
    st.cps = 15.50f;
    st.dose = NAN;
    len = af_ha_state_json(&st, 0x03, b, sizeof(b));
    CHECK(len == 0);
    
    if (failures == f0) printf("PASS t_state_invalid\n");
    else printf("FAIL t_state_invalid\n");
}

static void t_state_small_buf(void) { int f0 = failures;
    tests++;
    char b[1024];
    
    af_ha_state_t st = {
        .dose_rate = 0.0750f,
        .cps = 15.50f,
        .dose = 174.810f,
        .pulses_2s = 31,
        .battery = 27,
        .temp_af = 26,
        .air_temp = 26.5f,
        .air_press = 751.7f,
        .ts = 1792861680u
    };
    
    // Get full length first
    size_t full_len = af_ha_state_json(&st, 0x03, b, sizeof(b));
    CHECK(full_len > 0);
    
    // cap = full_len (not enough for null terminator)
    size_t len = af_ha_state_json(&st, 0x03, b, full_len);
    CHECK(len == 0);
    
    // cap = full_len + 1 (just enough)
    len = af_ha_state_json(&st, 0x03, b, full_len + 1);
    CHECK(len == full_len);
    
    if (failures == f0) printf("PASS t_state_small_buf\n");
    else printf("FAIL t_state_small_buf\n");
}

int main(void) {
    t_valid_id();
    t_topics();
    t_topic_small_buf();
    t_bad_dev();
    t_escape();
    t_config_dose_rate();
    t_config_optional_fields();
    t_sensor_table();
    t_state_full();
    t_state_caps();
    t_state_invalid();
    t_state_small_buf();
    
    printf("TESTS %d FAILURES %d\n", tests, failures);
    return failures != 0;
}
