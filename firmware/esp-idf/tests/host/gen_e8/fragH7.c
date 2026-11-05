static void sec7_json(void)
{
    af_push_cfg_t c_base;
    af_push_view_t v_base;

    af_push_cfg_default(&c_base);
    c_base.enabled = 1;
    strcpy(c_base.host, "10.20.30.40");
    strcpy(c_base.token, "AbCdEf0123456789xyz");
    c_base.port = 8480;
    c_base.interval_s = 60;
    c_base.batch_rows = 500;

    memset(&v_base, 0, sizeof v_base);
    v_base.now_ts = 1792860030;
    v_base.stack_hwm = 1800;
    for (int i = 0; i < 3; i++) {
        af_push_ring_init(&v_base.ring[i], 500);
    }
    v_base.info[0] = (af_push_ringinfo_t){1, 0, 1200, 21};
    v_base.info[1] = (af_push_ringinfo_t){1, 0, 365, 21};
    v_base.info[2] = (af_push_ringinfo_t){1, 0, 291, 21};

    int n;

    // 0. полная доставка
    {
        af_push_cfg_t c = c_base;
        af_push_view_t v = v_base;
        v.ring[0].cur = (af_push_cur_t){1199, 21, 1};
        v.ring[1].cur = (af_push_cur_t){364, 21, 1};
        v.ring[2].cur = (af_push_cur_t){290, 21, 1};
        v.ring[0].sent_total = 1200;
        v.ring[1].sent_total = 365;
        v.ring[2].sent_total = 291;
        v.ring[0].last_ok_ts = 1792860000;
        v.ring[1].last_ok_ts = 1792860000;
        v.ring[2].last_ok_ts = 1792860000;
        v.ring[0].last_rows = 100;
        v.ring[1].last_rows = 100;
        v.ring[2].last_rows = 100;
        v.ring[0].last_http = 200;
        v.ring[1].last_http = 200;
        v.ring[2].last_http = 200;
        n = af_push_json(OUT, sizeof OUT, &c, &v);
        printf("json id=0 n=%d crc=%08lx\n", n, n > 0 ? h32(OUT, strlen(OUT)) : 0UL);
    }

    // 1. середина доставки
    {
        af_push_cfg_t c = c_base;
        af_push_view_t v = v_base;
        v.ring[0].cur = (af_push_cur_t){599, 21, 1};
        v.ring[1].cur = (af_push_cur_t){199, 21, 1};
        v.ring[2].cur = (af_push_cur_t){99, 21, 1};
        v.ring[0].sent_total = 600;
        v.ring[1].sent_total = 200;
        v.ring[2].sent_total = 100;
        v.ring[0].last_ok_ts = 1792860000;
        v.ring[1].last_ok_ts = 1792860000;
        v.ring[2].last_ok_ts = 1792860000;
        v.ring[0].last_rows = 100;
        v.ring[1].last_rows = 100;
        v.ring[2].last_rows = 100;
        v.ring[0].last_http = 200;
        v.ring[1].last_http = 200;
        v.ring[2].last_http = 200;
        v.sending = 1;
        n = af_push_json(OUT, sizeof OUT, &c, &v);
        printf("json id=1 n=%d crc=%08lx\n", n, n > 0 ? h32(OUT, strlen(OUT)) : 0UL);
    }

    // 2. ошибка 401
    {
        af_push_cfg_t c = c_base;
        af_push_view_t v = v_base;
        v.auth_stop = 1;
        v.ring[0].err = 3;
        v.ring[0].last_http = 401;
        v.ring[0].fail_total = 1;
        n = af_push_json(OUT, sizeof OUT, &c, &v);
        printf("json id=2 n=%d crc=%08lx\n", n, n > 0 ? h32(OUT, strlen(OUT)) : 0UL);
    }

    // 3. кольцо agg остановлено
    {
        af_push_cfg_t c = c_base;
        af_push_view_t v = v_base;
        v.ring[0].cur = (af_push_cur_t){1199, 21, 1};
        v.ring[1].cur = (af_push_cur_t){364, 21, 1};
        v.ring[2].cur = (af_push_cur_t){290, 21, 1};
        v.ring[0].sent_total = 1200;
        v.ring[1].sent_total = 365;
        v.ring[2].sent_total = 291;
        v.ring[0].last_ok_ts = 1792860000;
        v.ring[1].last_ok_ts = 1792860000;
        v.ring[2].last_ok_ts = 1792860000;
        v.ring[0].last_rows = 100;
        v.ring[1].last_rows = 100;
        v.ring[2].last_rows = 100;
        v.ring[0].last_http = 200;
        v.ring[1].last_http = 200;
        v.ring[2].last_http = 200;
        v.ring[1].stopped = 1;
        v.ring[1].err = 4;
        v.ring[1].last_http = 400;
        v.ring[1].cur = (af_push_cur_t){99, 21, 1};
        n = af_push_json(OUT, sizeof OUT, &c, &v);
        printf("json id=3 n=%d crc=%08lx\n", n, n > 0 ? h32(OUT, strlen(OUT)) : 0UL);
    }

    // 4. выключено
    {
        af_push_cfg_t c = c_base;
        af_push_view_t v = v_base;
        c.enabled = 0;
        v.ring[0].cur = (af_push_cur_t){1199, 21, 1};
        v.ring[1].cur = (af_push_cur_t){364, 21, 1};
        v.ring[2].cur = (af_push_cur_t){290, 21, 1};
        v.ring[0].sent_total = 1200;
        v.ring[1].sent_total = 365;
        v.ring[2].sent_total = 291;
        v.ring[0].last_ok_ts = 1792860000;
        v.ring[1].last_ok_ts = 1792860000;
        v.ring[2].last_ok_ts = 1792860000;
        v.ring[0].last_rows = 100;
        v.ring[1].last_rows = 100;
        v.ring[2].last_rows = 100;
        v.ring[0].last_http = 200;
        v.ring[1].last_http = 200;
        v.ring[2].last_http = 200;
        n = af_push_json(OUT, sizeof OUT, &c, &v);
        printf("json id=4 n=%d crc=%08lx\n", n, n > 0 ? h32(OUT, strlen(OUT)) : 0UL);
    }

    // 5. публичный адрес
    {
        af_push_cfg_t c = c_base;
        af_push_view_t v = v_base;
        strcpy(c.host, "8.8.8.8");
        v.ring[0].cur = (af_push_cur_t){1199, 21, 1};
        v.ring[1].cur = (af_push_cur_t){364, 21, 1};
        v.ring[2].cur = (af_push_cur_t){290, 21, 1};
        v.ring[0].sent_total = 1200;
        v.ring[1].sent_total = 365;
        v.ring[2].sent_total = 291;
        v.ring[0].last_ok_ts = 1792860000;
        v.ring[1].last_ok_ts = 1792860000;
        v.ring[2].last_ok_ts = 1792860000;
        v.ring[0].last_rows = 100;
        v.ring[1].last_rows = 100;
        v.ring[2].last_rows = 100;
        v.ring[0].last_http = 200;
        v.ring[1].last_http = 200;
        v.ring[2].last_http = 200;
        v.test = (af_push_test_t){2, 200, 15, 0, 0, 1792860020};
        n = af_push_json(OUT, sizeof OUT, &c, &v);
        printf("json id=5 n=%d crc=%08lx\n", n, n > 0 ? h32(OUT, strlen(OUT)) : 0UL);
    }
}
