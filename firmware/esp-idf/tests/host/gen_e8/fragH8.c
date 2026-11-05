static int send_one(const char *tag, int kind, const af_push_cfg_t *c, af_push_ring_t *r, uint8_t *auth, int *calls_total) {
    uint32_t left = 0;
    int moved = 0;
    int rc = af_push_send_ring(&FK_OPS, &FK, kind, c, D1_MAC, r, auth, BODY, sizeof BODY, &left, &moved);
    printf("send %s k=%d rc=%d mv=%d left=%lu acked=%lu v=%u calls=%d first=%lu lost=%lu epoch=%lu len=%lu crc=%08lx err=%u stop=%u eff=%u\n", tag, kind, rc, moved, (unsigned long)left, (unsigned long)r->cur.acked, (unsigned)r->cur.valid, FK.calls, (unsigned long)FK.last_first_seq, (unsigned long)FK.last_lost, (unsigned long)FK.last_epoch, (unsigned long)FK.last_len, h32(FK.last_body, FK.last_len), (unsigned)r->err, (unsigned)r->stopped, (unsigned)r->eff_batch);
    (*calls_total)++;
    return rc;
}

static void sec8_send(void) {
    af_push_cfg_t c;
    af_push_ring_t r[3];
    uint8_t auth = 0;
    int calls_total = 0;
    int k, rc, i;

    // (a) полная отправка до IDLE
    memset(&FK, 0, sizeof FK); FK.st = &ST; FK.http = 200; FK.auto_ok = 1; FK.now = 1792860000;
    st_init(&ST, 1300, 400, 400); st_fill(&ST, 1200, 365, 291);
    af_push_cfg_default(&c); c.enabled = 1; c.batch_rows = 100;
    for (k = 0; k < 3; k++) af_push_ring_init(&r[k], 100);
    auth = 0;
    for (i = 0; i < 100; i++) {
        int all_idle = 1;
        for (k = 0; k < 3; k++) {
            rc = send_one("a", k, &c, &r[k], &auth, &calls_total);
            if (rc != 1) all_idle = 0;
        }
        if (all_idle) break;
    }

    // (b) с ошибками транспорта и HTTP
    memset(&FK, 0, sizeof FK); FK.st = &ST; FK.http = 200; FK.auto_ok = 1; FK.now = 1792860000;
    st_init(&ST, 1300, 400, 400); st_fill(&ST, 1200, 365, 291);
    af_push_cfg_default(&c); c.enabled = 1; c.batch_rows = 100;
    for (k = 0; k < 3; k++) af_push_ring_init(&r[k], 100);
    auth = 0;
    calls_total = 0;
    for (i = 0; i < 60; i++) {
        int all_idle = 1;
        for (k = 0; k < 3; k++) {
            if (calls_total + 1 == 3) { FK.http = 503; FK.transport = 0; }
            else if (calls_total + 1 == 5) { FK.http = 200; FK.transport = 1; }
            else if (calls_total + 1 == 8) { FK.http = 413; FK.transport = 0; }
            else { FK.http = 200; FK.transport = 0; }
            rc = send_one("b", k, &c, &r[k], &auth, &calls_total);
            if (rc != 1) all_idle = 0;
        }
        if (all_idle) break;
    }

    // (c) 401 и повтор
    memset(&FK, 0, sizeof FK); FK.st = &ST; FK.http = 200; FK.auto_ok = 1; FK.now = 1792860000;
    st_init(&ST, 1300, 400, 400); st_fill(&ST, 1200, 365, 291);
    af_push_cfg_default(&c); c.enabled = 1; c.batch_rows = 100;
    for (k = 0; k < 3; k++) af_push_ring_init(&r[k], 100);
    auth = 0;
    FK.http = 401;
    send_one("c", 0, &c, &r[0], &auth, &calls_total);
    FK.http = 200;
    send_one("c", 0, &c, &r[0], &auth, &calls_total);

    // (d) сброс курсора и повтор
    memset(&FK, 0, sizeof FK); FK.st = &ST; FK.http = 200; FK.auto_ok = 1; FK.now = 1792860000;
    st_init(&ST, 1300, 400, 400); st_fill(&ST, 1200, 365, 291);
    af_push_cfg_default(&c); c.enabled = 1; c.batch_rows = 100;
    for (k = 0; k < 3; k++) af_push_ring_init(&r[k], 100);
    auth = 0;
    for (i = 0; i < 40; i++) {
        rc = send_one("d", 0, &c, &r[0], &auth, &calls_total);
        if (rc == 1) break;
    }
    af_push_ring_reset_cursor(&r[0], 100);
    strcpy(FK.resp, "{\"accepted\":0,\"last_seq\":1199}");
    send_one("d", 0, &c, &r[0], &auth, &calls_total);
    FK.resp[0] = '\0';
    send_one("d", 0, &c, &r[0], &auth, &calls_total);

    // (e) поврежденные записи
    memset(&FK, 0, sizeof FK); FK.st = &ST; FK.http = 200; FK.auto_ok = 1; FK.now = 1792860000;
    st_init(&ST, 1300, 400, 400); st_fill(&ST, 30, 0, 0);
    ST.buf[0][0*AF_REC_RAW_SIZE+18] ^= 0xFF;
    ST.buf[0][7*AF_REC_RAW_SIZE+18] ^= 0xFF;
    af_push_cfg_default(&c); c.enabled = 1; c.batch_rows = 100;
    for (k = 0; k < 3; k++) af_push_ring_init(&r[k], 100);
    auth = 0;
    for (i = 0; i < 10; i++) {
        rc = send_one("e", 0, &c, &r[0], &auth, &calls_total);
        if (rc == 1) break;
    }
}
