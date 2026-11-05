static void sec3_res(void)
{
    struct {
        const char *id;
        int http;
        int terr;
        uint32_t start;
        uint32_t rows;
        const char *body;
        int eff_override;
    } cases[] = {
        {"b0", 200, 0, 0, 100, "{\"accepted\":100,\"last_seq\":99}", -1},
        {"b1", 200, 0, 100, 100, "{\"accepted\":100,\"last_seq\":199}", -1},
        {"b2", 200, 0, 200, 100, "{\"accepted\":100,\"last_seq\":299}", -1},
        {"b3", 200, 0, 300, 100, "{\"accepted\":100,\"last_seq\":399}", -1},
        {"b4", 200, 0, 400, 100, "{\"accepted\":100,\"last_seq\":499}", -1},
        {"b5", 200, 0, 500, 100, "{\"accepted\":100,\"last_seq\":599}", -1},
        {"b6", 200, 0, 600, 100, "{\"accepted\":100,\"last_seq\":699}", -1},
        {"b7", 200, 0, 700, 100, "{\"accepted\":100,\"last_seq\":799}", -1},
        {"b8", 200, 0, 800, 100, "{\"accepted\":100,\"last_seq\":899}", -1},
        {"b9", 200, 0, 900, 100, "{\"accepted\":100,\"last_seq\":999}", -1},
        {"b10", 200, 0, 1000, 100, "{\"accepted\":100,\"last_seq\":1099}", -1},
        {"b11", 200, 0, 1100, 100, "{\"accepted\":100,\"last_seq\":1199}", -1},
        {"idem", 200, 0, 0, 100, "{\"accepted\":0,\"last_seq\":1199}", -1},
        {"empty", 200, 0, 0, 0, "{\"accepted\":0,\"last_seq\":-1}", -1},
        {"bad_acc", 200, 0, 100, 100, "{\"accepted\":101,\"last_seq\":199}", -1},
        {"bad_low", 200, 0, 100, 100, "{\"accepted\":0,\"last_seq\":50}", -1},
        {"bad_high", 200, 0, 1100, 100, "{\"accepted\":100,\"last_seq\":1300}", -1},
        {"junk", 200, 0, 0, 0, "OK", -1},
        {"h400", 400, 0, 100, 100, "{\"error\":\"x\"}", -1},
        {"h401", 401, 0, 100, 100, "{\"error\":\"x\"}", -1},
        {"h404", 404, 0, 100, 100, "{\"error\":\"x\"}", -1},
        {"h405", 405, 0, 100, 100, "{\"error\":\"x\"}", -1},
        {"h408", 408, 0, 100, 100, "{\"error\":\"x\"}", -1},
        {"h413", 413, 0, 100, 100, "{\"error\":\"x\"}", -1},
        {"h429", 429, 0, 100, 100, "{\"error\":\"x\"}", -1},
        {"h500", 500, 0, 100, 100, "{\"error\":\"x\"}", -1},
        {"h502", 502, 0, 100, 100, "{\"error\":\"x\"}", -1},
        {"h503", 503, 0, 100, 100, "{\"error\":\"x\"}", -1},
        {"h599", 599, 0, 100, 100, "{\"error\":\"x\"}", -1},
        {"h301", 301, 0, 100, 100, "{\"error\":\"x\"}", -1},
        {"h204", 204, 0, 100, 100, "{\"error\":\"x\"}", -1},
        {"net", 0, 1, 100, 100, "", -1},
        {"huge413", 413, 0, 100, 100, "{\"error\":\"x\"}", 10},
        {"eff0", 0, 1, 100, 100, "", 0}
    };

    size_t n = sizeof(cases) / sizeof(cases[0]);
    for (size_t i = 0; i < n; i++) {
        af_push_ring_t r;
        af_push_ring_init(&r, 500);
        r.cur.acked = 99;
        r.cur.epoch = 21;
        r.cur.valid = 1;
        uint8_t auth = 0;

        if (cases[i].eff_override != -1) {
            r.eff_batch = (uint16_t)cases[i].eff_override;
        }

        int act = af_push_on_result(
            &r, &auth,
            cases[i].http,
            cases[i].terr,
            (const uint8_t *)cases[i].body,
            strlen(cases[i].body),
            cases[i].start,
            cases[i].rows,
            1200,
            21,
            500,
            1792860000
        );

        printf("res id=%s act=%d acked=%lu v=%u ep=%lu err=%u stop=%u fails=%u http=%d eff=%u sent=%lu failt=%lu retr=%lu auth=%u\n",
               cases[i].id,
               act,
               (unsigned long)r.cur.acked,
               (unsigned)r.cur.valid,
               (unsigned long)r.cur.epoch,
               (unsigned)r.err,
               (unsigned)r.stopped,
               (unsigned)r.fails,
               (int)r.last_http,
               (unsigned)r.eff_batch,
               (unsigned long)r.sent_total,
               (unsigned long)r.fail_total,
               (unsigned long)r.retries,
               (unsigned)auth);
    }
}
