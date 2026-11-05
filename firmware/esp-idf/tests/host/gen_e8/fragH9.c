static void sec9_more(void) {
    const char *hosts[] = {"10.20.30.40", "10.0.0.5", "172.20.5.9", "172.16.0.1", "172.31.9.9", "172.32.0.1", "collector.lan", "af-ingest.local", "pi5", "8.8.8.8", "256.1.1.1", "01.2.3.4", "1.2.3", "host name", "-bad", "bad-", "a..b", "10.20.30.40.", "12345", "10.20.30.040", "127.0.0.1", "169.254.7.7"};
    for (size_t i = 0; i < sizeof(hosts)/sizeof(hosts[0]); i++) printf("host s=%s ok=%d cls=%d\n", hosts[i], af_push_host_ok(hosts[i]), af_push_addr_class(hosts[i]));

    char tok64[65], tok65[66]; memset(tok64, 'k', 64); tok64[64] = 0; memset(tok65, 'k', 65); tok65[65] = 0;
    const char *toks[] = {"AbCdEf0123456789xyzQWERT", tok64, tok65, "has space", "back\\slash", "quo\"te", "", "tab\there"};
    for (size_t i = 0; i < sizeof(toks)/sizeof(toks[0]); i++) printf("tok id=%d ok=%d\n", (int)i, af_push_token_ok(toks[i]));

    af_push_cfg_t base; af_push_cfg_default(&base); base.enabled = 1; strcpy(base.host, "10.20.30.40"); strcpy(base.token, "AbCdEf0123456789xyzQWERT");
    int id = 0;
    int ports[] = {0, 1, 8480, 65535}; for (int i = 0; i < 4; i++) { af_push_cfg_t c = base; c.port = ports[i]; printf("valid id=%d rc=%d cmpl=%d\n", id++, af_push_cfg_valid(&c), af_push_cfg_complete(&c)); }
    int ints[] = {9, 10, 60, 3600, 3601}; for (int i = 0; i < 5; i++) { af_push_cfg_t c = base; c.interval_s = ints[i]; printf("valid id=%d rc=%d cmpl=%d\n", id++, af_push_cfg_valid(&c), af_push_cfg_complete(&c)); }
    int bats[] = {49, 50, 500, 2000, 2001}; for (int i = 0; i < 5; i++) { af_push_cfg_t c = base; c.batch_rows = bats[i]; printf("valid id=%d rc=%d cmpl=%d\n", id++, af_push_cfg_valid(&c), af_push_cfg_complete(&c)); }
    int tos[] = {1, 2, 8, 30, 31}; for (int i = 0; i < 5; i++) { af_push_cfg_t c = base; c.timeout_s = tos[i]; printf("valid id=%d rc=%d cmpl=%d\n", id++, af_push_cfg_valid(&c), af_push_cfg_complete(&c)); }
    int rings[] = {0, 1, 5, 7, 8}; for (int i = 0; i < 5; i++) { af_push_cfg_t c = base; c.rings = rings[i]; printf("valid id=%d rc=%d cmpl=%d\n", id++, af_push_cfg_valid(&c), af_push_cfg_complete(&c)); }
    int ens[] = {0, 1, 2}; for (int i = 0; i < 3; i++) { af_push_cfg_t c = base; c.enabled = ens[i]; printf("valid id=%d rc=%d cmpl=%d\n", id++, af_push_cfg_valid(&c), af_push_cfg_complete(&c)); }
    { af_push_cfg_t c = base; c.host[10] = 'X'; printf("valid id=%d rc=%d cmpl=%d\n", id++, af_push_cfg_valid(&c), af_push_cfg_complete(&c)); }
    { af_push_cfg_t c = base; c.token[24] = 'X'; printf("valid id=%d rc=%d cmpl=%d\n", id++, af_push_cfg_valid(&c), af_push_cfg_complete(&c)); }

    af_push_cfg_t dc; af_push_cfg_default(&dc); dc.enabled = 1; strcpy(dc.host, "10.20.30.40"); strcpy(dc.token, "AbCdEf0123456789xyzQWERT"); dc.port = 8480; dc.interval_s = 60; dc.batch_rows = 500; dc.timeout_s = 8; dc.rings = 7;
    uint8_t blob[152]; size_t blen = af_push_cfg_encode(&dc, blob, 152);
    int ps[] = {0,3,4,5,6,7,8,9,10,12,14,16,17,18,30,81,82,100,146,147,148,151};
    for (int i = 0; i < 22; i++) {
        for (int fix = 0; fix < 2; fix++) {
            uint8_t b[152]; memcpy(b, blob, 152); b[ps[i]] ^= 0x01;
            if (fix) { uint32_t crc = af_bsn_crc32(b, 148); b[148] = crc & 0xFF; b[149] = (crc >> 8) & 0xFF; b[150] = (crc >> 16) & 0xFF; b[151] = (crc >> 24) & 0xFF; }
            af_push_cfg_t d; bool ok = af_push_cfg_decode(&d, b, 152);
            printf("dec p=%d fix=%d ok=%d port=%u en=%u int=%u\n", ps[i], fix, ok, d.port, d.enabled, d.interval_s);
        }
    }
    { af_push_cfg_t d; bool ok = af_push_cfg_decode(&d, blob, 0); printf("decl n=%d ok=%d\n", 0, ok); }
    { af_push_cfg_t d; bool ok = af_push_cfg_decode(&d, blob, 151); printf("decl n=%d ok=%d\n", 151, ok); }
    { uint8_t b153[153]; memcpy(b153, blob, 152); b153[152] = 0; af_push_cfg_t d; bool ok = af_push_cfg_decode(&d, b153, 153); printf("decl n=%d ok=%d\n", 153, ok); }
    { af_push_cfg_t d; bool ok = af_push_cfg_decode(&d, NULL, 152); printf("decl n=%d ok=%d\n", -1, ok); }

    af_push_cur_t curs[3] = {{1199,21,1},{364,21,1},{290,21,1}};
    uint8_t cblob[48]; size_t clen = af_push_curs_encode(curs, cblob, 48);
    int cps[] = {0,3,4,6,8,11,12,16,17,18,19,20,28,32,36,40,44,47};
    for (int i = 0; i < 18; i++) {
        for (int fix = 0; fix < 2; fix++) {
            uint8_t b[48]; memcpy(b, cblob, 48); b[cps[i]] ^= 0x01;
            if (fix) { uint32_t crc = af_bsn_crc32(b, 44); b[44] = crc & 0xFF; b[45] = (crc >> 8) & 0xFF; b[46] = (crc >> 16) & 0xFF; b[47] = (crc >> 24) & 0xFF; }
            af_push_cur_t d[3]; bool ok = af_push_curs_decode(d, b, 48);
            printf("cdec p=%d fix=%d ok=%d a0=%lu v0=%u v1=%u v2=%u\n", cps[i], fix, ok, (unsigned long)d[0].acked, d[0].valid, d[1].valid, d[2].valid);
        }
    }
    { af_push_cur_t d[3]; bool ok = af_push_curs_decode(d, cblob, 0); printf("cdec n=%d ok=%d\n", 0, ok); }
    { af_push_cur_t d[3]; bool ok = af_push_curs_decode(d, cblob, 47); printf("cdec n=%d ok=%d\n", 47, ok); }
    { uint8_t b49[49]; memcpy(b49, cblob, 48); b49[48] = 0; af_push_cur_t d[3]; bool ok = af_push_curs_decode(d, b49, 49); printf("cdec n=%d ok=%d\n", 49, ok); }
    { af_push_cur_t d[3]; bool ok = af_push_curs_decode(d, NULL, 48); printf("cdec n=%d ok=%d\n", -1, ok); }

    int dirty[] = {0, 1};
    uint32_t nows[] = {100000, 29999, 30000, 30001, 60000, 0x7000, 0x7F00, 5};
    uint32_t lasts[] = {0, 0, 0, 0, 0, 0xFFFFFF00, 0xFFFFFF00, 6};
    for (int i = 0; i < 8; i++) {
        for (int d = 0; d < 2; d++) {
            bool v = af_push_curs_due(d, nows[i], lasts[i]);
            printf("due d=%d now=%lu last=%lu v=%d\n", d, (unsigned long)nows[i], (unsigned long)lasts[i], v);
        }
    }

    int sf[] = {{0,0},{1,0},{0,2},{0,3},{0,255}};
    int rgs[] = {7, 5, 3, 0};
    for (int i = 0; i < 5; i++) {
        for (int r = 0; r < 3; r++) {
            for (int rg = 0; rg < 4; rg++) {
                for (int a = 0; a < 2; a++) {
                    af_push_ring_t rr[3]; memset(rr, 0, sizeof(rr));
                    rr[r].stopped = sf[i][0]; rr[r].fails = sf[i][1];
                    int v = af_push_bad(rr, rgs[rg], a);
                    printf("bad k=%d s=%d f=%d rings=%d auth=%d v=%d\n", r, sf[i][0], sf[i][1], rgs[rg], a, v);
                }
            }
        }
    }
    for (int e = 0; e < 2; e++) for (int s = 0; s < 2; s++) for (int b = 0; b < 2; b++) printf("state e=%d s=%d b=%d n=%s\n", e, s, b, af_push_state_name(e, s, b));
    for (int e = 0; e <= 12; e++) printf("errn e=%d n=%s\n", e, af_push_err_name((uint8_t)e));
    printf("errn e=%d n=%s\n", 255, af_push_err_name(255));

    int effs[] = {10, 25, 50, 125, 250, 300, 500};
    for (int i = 0; i < 7; i++) {
        af_push_ring_t r; af_push_ring_init(&r, 500); r.eff_batch = effs[i]; r.cur = (af_push_cur_t){99, 21, 1};
        uint8_t auth = 0;
        int act = af_push_on_result(&r, &auth, 200, 0, "{\"accepted\":0,\"last_seq\":99}", 26, 100, 0, 1200, 21, 500, 0);
        printf("grow eff=%d act=%d -> %u\n", effs[i], act, r.eff_batch);
    }
    {
        af_push_ring_t r; af_push_ring_init(&r, 500); r.eff_batch = 500;
        uint8_t auth = 0;
        for (int i = 0; i < 10; i++) {
            int act = af_push_on_result(&r, &auth, 413, 0, "", 0, 100, 100, 1200, 21, 500, 0);
            printf("s413 i=%d act=%d eff=%u stop=%u retr=%lu failt=%lu\n", i, act, r.eff_batch, r.stopped, (unsigned long)r.retries, (unsigned long)r.fail_total);
        }
    }
    {
        af_push_ring_t r; af_push_ring_init(&r, 500); r.fails = 253;
        uint8_t auth = 0;
        for (int i = 0; i < 4; i++) {
            int act = af_push_on_result(&r, &auth, 503, 0, "", 0, 100, 100, 1200, 21, 500, 0);
            printf("s503 i=%d act=%d fails=%u failt=%lu\n", i, act, r.fails, (unsigned long)r.fail_total);
        }
    }
    {
        af_push_ring_t r; af_push_ring_init(&r, 500); r.fails = 254;
        uint8_t auth = 0;
        for (int i = 0; i < 3; i++) {
            int act = af_push_on_result(&r, &auth, 0, 1, "", 0, 100, 100, 1200, 21, 500, 0);
            printf("snet i=%d fails=%u\n", i, r.fails);
        }
    }

    const char *bodies[] = {
        "{\"accepted\":5,\"last_seq\":104}",
        "{\"accepted\":0,\"last_seq\":-1}",
        "{\"accepted\":0,\"last_seq\":4294967295}",
        "{\"last_seq\":7,\"accepted\":3}",
        "{\"accepted\":1,\"last_seq\":-2}",
        "{\"accepted\":4294967296,\"last_seq\":0}",
        "{\"accepted\":1.5,\"last_seq\":2}",
        "{\"accepted\":1}",
        "{\"accepted\":1,\"last_seq\":2,\"x\":3}",
        "[]",
        "OK",
        "",
        "{\"accepted\":\"1\",\"last_seq\":2}",
        "{\"accepted\":-1,\"last_seq\":2}"
    };
    for (int i = 0; i < 14; i++) {
        uint32_t acc = 77; int64_t last = 77;
        int rc = af_push_parse_resp(bodies[i], strlen(bodies[i]), &acc, &last);
        printf("resp id=%d rc=%d acc=%lu last=%ld\n", i, rc, (unsigned long)acc, (long)last);
    }

    uint32_t olds[] = {0, 900, 0xFFFFFFF0};
    uint32_t heads[] = {1200, 1200, 0x10};
    for (int i = 0; i < 3; i++) {
        uint32_t o = olds[i], h = heads[i];
        uint32_t acks[] = {o-2, o-1, o, o+1, h-2, h-1, h, h+1};
        for (int j = 0; j < 8; j++) {
            af_push_cur_t cur = {acks[j], 21, 1};
            af_push_plan_t p; af_push_plan(&cur, o, h, 21, 100, &p);
            printf("plan2 o=%lu h=%lu a=%lu start=%lu cnt=%lu lost=%lu pend=%lu fl=%u\n", (unsigned long)o, (unsigned long)h, (unsigned long)acks[j], (unsigned long)p.start, (unsigned long)p.count, (unsigned long)p.lost, (unsigned long)p.pending, p.flags);
        }
        { af_push_cur_t cur = {0, 21, 0}; af_push_plan_t p; af_push_plan(&cur, o, h, 21, 100, &p); printf("plan2 o=%lu h=%lu a=%lu start=%lu cnt=%lu lost=%lu pend=%lu fl=%u\n", (unsigned long)o, (unsigned long)h, (unsigned long)cur.acked, (unsigned long)p.start, (unsigned long)p.count, (unsigned long)p.lost, (unsigned long)p.pending, p.flags); }
        { af_push_cur_t cur = {o, 20, 1}; af_push_plan_t p; af_push_plan(&cur, o, h, 21, 100, &p); printf("plan2 o=%lu h=%lu a=%lu start=%lu cnt=%lu lost=%lu pend=%lu fl=%u\n", (unsigned long)o, (unsigned long)h, (unsigned long)cur.acked, (unsigned long)p.start, (unsigned long)p.count, (unsigned long)p.lost, (unsigned long)p.pending, p.flags); }
    }

    af_push_cfg_t sc; af_push_cfg_default(&sc); sc.enabled = 1; sc.batch_rows = 100;
    af_push_ring_t sr; af_push_ring_init(&sr, 100);
    uint8_t sauth = 0;
    uint32_t sleft = 0; int smv = 0;
    int sids[] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20};
    for (int i = 0; i < 20; i++) {
        memset(&FK, 0, sizeof(FK)); FK.st = &ST; FK.http = 200; FK.auto_ok = 1; FK.now = 1792860000;
        st_init(&ST, 1300, 400, 400); st_fill(&ST, 1200, 365, 291);
        memset(&sr, 0, sizeof(sr)); af_push_ring_init(&sr, 100); sauth = 0; sleft = 0; smv = 0;
        int kind = 0;
        switch (i) {
            case 0: st_init(&ST, 300, 400, 400); st_fill(&ST, 1200, 0, 0); sr.cur = (af_push_cur_t){49, 21, 1}; break;
            case 1: sr.cur = (af_push_cur_t){500, 20, 1}; break;
            case 2: sr.cur = (af_push_cur_t){5000, 21, 1}; break;
            case 3: ST.busy = 1; break;
            case 4: ST.rbusy = 1; break;
            case 5: break;
            case 6: break;
            case 7: st_fill(&ST, 5, 0, 0); for (int j = 0; j < 5; j++) ST.buf[0][j*AF_REC_RAW_SIZE+18] ^= 0xFF; break;
            case 8: st_fill(&ST, 20, 0, 0); ST.buf[0][18] ^= 0xFF; break;
            case 9: sr.eff_batch = 500; break;
            case 10: sr.stopped = 1; break;
            case 11: strcpy(FK.resp, "{\"accepted\":0,\"last_seq\":1199}"); sr.cur.valid = 0; break;
            case 12: strcpy(FK.resp, "zzz"); break;
            case 13: FK.http = 400; break;
            case 14: FK.http = 401; break;
            case 15: FK.http = 500; break;
            case 16: FK.http = 413; break;
            case 17: FK.transport = 1; break;
            case 18: kind = 1; break;
            case 19: kind = 2; break;
        }
        const char *mac = (i == 5) ? "" : (i == 6) ? NULL : D1_MAC;
        int rc = af_push_send_ring(&FK_OPS, &FK, kind, &sc, mac, &sr, &sauth, BODY, sizeof(BODY), &sleft, &smv);
        uint32_t crc = FK.last_len ? af_bsn_crc32((uint8_t*)FK.last_body, FK.last_len) : 0;
        printf("sendx id=%d rc=%d mv=%d left=%lu acked=%lu v=%u calls=%d first=%lu lost=%lu epoch=%lu len=%lu crc=%08lx err=%u stop=%u eff=%u auth=%u\n", i+1, rc, smv, (unsigned long)sleft, (unsigned long)sr.cur.acked, sr.cur.valid, FK.calls, (unsigned long)FK.last_first_seq, (unsigned long)FK.last_lost, (unsigned long)FK.last_epoch, (unsigned long)FK.last_len, crc, sr.err, sr.stopped, sr.eff_batch, sauth);
    }

    af_push_cfg_t jc; af_push_cfg_default(&jc); jc.enabled = 1; strcpy(jc.host, "10.20.30.40"); strcpy(jc.token, "AbCdEf0123456789xyzQWERT");
    af_push_view_t jv; memset(&jv, 0, sizeof(jv));
    jv.info[0] = (af_push_view_info_t){1, 0, 1200, 21}; jv.info[1] = (af_push_view_info_t){1, 0, 365, 21}; jv.info[2] = (af_push_view_info_t){1, 0, 291, 21};
    jv.ring[0].cur = (af_push_cur_t){599, 21, 1}; jv.ring[0].last_ok_ts = 1792860000; jv.now_ts = 1792860030;
    int jn = af_push_json(OUT, sizeof(OUT), &jc, &jv);
    int jleak = (strstr(OUT, "AbCdEf") != NULL);
    printf("jsonx len=%d leak=%d\n", jn, jleak);
    char OUT2[8192];
    int caps[] = {jn-1, jn, jn+1, jn+2, 10, 0};
    for (int i = 0; i < 6; i++) {
        if (caps[i] > 8192) caps[i] = 8192;
        if (caps[i] < 0) caps[i] = 0;
        printf("jsonx cap=%lu rc=%d\n", (unsigned long)caps[i], af_push_json(OUT2, caps[i], &jc, &jv));
    }

    st_init(&ST, 1300, 400, 400); st_fill(&ST, 1200, 365, 291);
    af_push_pack_t pk; memset(&pk, 0, sizeof(pk));
    int pids[] = {1,2,3,4,5,6,7,8};
    for (int i = 0; i < 8; i++) {
        int kind = 0; uint32_t maxr = 100; size_t cap = 416; const char *mac = D1_MAC;
        switch (i) {
            case 0: kind = 3; break;
            case 1: kind = -1; break;
            case 2: maxr = 0; break;
            case 3: cap = 415; break;
            case 4: cap = 416; break;
            case 5: mac = ""; break;
            case 6: mac = "a,b"; break;
            case 7: mac = NULL; break;
        }
        int rc = af_push_pack(ld_all, &ST, kind, 0, maxr, mac, 21, 0, BODY, cap, &pk);
        printf("packx id=%d rc=%d rows=%lu scan=%lu\n", i+1, rc, (unsigned long)pk.rows, (unsigned long)pk.scan_end);
    }
}
