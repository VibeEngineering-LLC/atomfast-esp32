static esp_err_t handle_push_get(httpd_req_t *req)
{
    char *buf = heap_caps_malloc(2048, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = malloc(2048);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
        return ESP_FAIL;
    }

    int n = af_push_get_json(buf, 2048);
    if (n < 0) {
        free(buf);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t e = httpd_resp_send(req, buf, n);
    free(buf);
    return e;
}

static esp_err_t handle_push_post(httpd_req_t *req)
{
    if (!csrf_check(req)) return ESP_FAIL;

    char body[384];
    int n = sens_read_body(req, body, sizeof body);
    if (n < 0) return sens_reply_err(req, "400 Bad Request", "bad_body");

    af_push_cfg_t cur;
    af_push_get_cfg(&cur);
    af_push_cmd_t cmd;
    int pc = af_push_parse(body, (size_t)n, &cur, &cmd);
    memset(body, 0, sizeof body);

    if (pc != 0) {
        memset(&cur, 0, sizeof cur);
        memset(&cmd, 0, sizeof cmd);
        if (pc == AF_PUSH_E_BODY) return sens_reply_err(req, "400 Bad Request", "bad_body");
        if (pc == AF_PUSH_E_JSON) return sens_reply_err(req, "400 Bad Request", "json");
        if (pc == AF_PUSH_E_KEY) return sens_reply_err(req, "400 Bad Request", "unknown_key");
        if (pc == AF_PUSH_E_VALUE) return sens_reply_err(req, "400 Bad Request", "bad_value");
        if (pc == AF_PUSH_E_INCOMPLETE) return sens_reply_err(req, "409 Conflict", "incomplete");
        return sens_reply_err(req, "400 Bad Request", "bad_value");
    }

    int rc = af_push_apply(&cmd);
    memset(&cur, 0, sizeof cur);
    memset(&cmd, 0, sizeof cmd);

    if (rc == -1) return sens_reply_err(req, "503 Service Unavailable", "not_ready");

    httpd_resp_set_type(req, "application/json");
    if (rc == 0) {
        httpd_resp_sendstr(req, "{\"ok\":true,\"persisted\":true}");
    } else {
        httpd_resp_sendstr(req, "{\"ok\":true,\"persisted\":false}");
    }
    return ESP_OK;
}

static esp_err_t handle_push_test(httpd_req_t *req)
{
    if (!csrf_check(req)) return ESP_FAIL;

    if (req->content_len > 0) {
        char tb[64];
        int n = sens_read_body(req, tb, sizeof tb);
        if (n < 0) return sens_reply_err(req, "400 Bad Request", "bad_body");
        if (!af_sens_body_ok(tb, (size_t)n) || !(n == 2 && memcmp(tb, "{}", 2) == 0)) {
            return sens_reply_err(req, "400 Bad Request", "bad_body");
        }
    }

    int rc = af_push_test_start();
    if (rc == 0) {
        httpd_resp_set_status(req, "202 Accepted");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"ok\":true,\"started\":true}");
        return ESP_OK;
    }
    if (rc == -1) return sens_reply_err(req, "409 Conflict", "busy");
    if (rc == -2) return sens_reply_err(req, "409 Conflict", "incomplete");
    return sens_reply_err(req, "503 Service Unavailable", "not_ready");
}

static esp_err_t handle_push_reset(httpd_req_t *req)
{
    if (!csrf_check(req)) return ESP_FAIL;

    char rb[64];
    int n = sens_read_body(req, rb, sizeof rb);
    if (n < 0) return sens_reply_err(req, "400 Bad Request", "bad_body");
    if (!af_sens_body_ok(rb, (size_t)n)) return sens_reply_err(req, "400 Bad Request", "bad_body");

    cJSON *root = cJSON_ParseWithLength(rb, (size_t)n);
    if (!root || !cJSON_IsObject(root)) {
        if (root) cJSON_Delete(root);
        return sens_reply_err(req, "400 Bad Request", "json");
    }

    const cJSON *c = cJSON_GetObjectItemCaseSensitive(root, "confirm");
    if (!(cJSON_GetArraySize(root) == 1 && cJSON_IsTrue(c))) {
        cJSON_Delete(root);
        return sens_reply_err(req, "400 Bad Request", "confirm_required");
    }

    cJSON_Delete(root);
    int rc = af_push_reset();
    if (rc != 0) return sens_reply_err(req, "503 Service Unavailable", "not_ready");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}


