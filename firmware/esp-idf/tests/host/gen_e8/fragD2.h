#define AF_PUSH_S_OK 0     /* пачка принята (или локально пропущены непригодные записи): можно сразу повторить */
#define AF_PUSH_S_IDLE 1   /* слать нечего */
#define AF_PUSH_S_WAIT 2   /* нужна пауза: ошибка, занято, нет данных для упаковки */
#define AF_PUSH_S_STOP 3   /* кольцо остановлено или остановлен доступ (401) */

typedef struct {
    int (*info)(void *ctx, int kind, uint32_t *oldest, uint32_t *head, uint32_t *epoch);   /* 0 ок, -1 занято, -2 кольца нет */
    af_push_read_fn read;
    int (*post)(void *ctx, int kind, const char *mac, uint32_t head, uint32_t epoch, uint32_t first_seq, uint32_t lost, const char *body, size_t len, int *http, char *resp, size_t resp_cap);   /* 0 - ответ получен (код в *http), иначе ошибка транспорта; resp - тело ответа, завершить нулём */
    uint32_t (*now_ts)(void *ctx);   /* unix-время или 0 */
} af_push_ops_t;

static inline int af_push_send_ring(const af_push_ops_t *ops, void *ctx, int kind, const af_push_cfg_t *c, const char *mac, af_push_ring_t *r, uint8_t *auth_stop, char *body, size_t cap, uint32_t *pending_left, int *cursor_moved) {
    if (!ops || !ops->info || !ops->read || !ops->post || !c || !r || !body || !pending_left || !cursor_moved || kind < 0 || kind > 2) {
        if (pending_left) *pending_left = 0;
        if (cursor_moved) *cursor_moved = 0;
        return AF_PUSH_S_WAIT;
    }
    *pending_left = 0;
    *cursor_moved = 0;
    if (r->stopped) return AF_PUSH_S_STOP;
    if (!mac || mac[0] == 0) {
        r->err = AF_PUSH_ERR_NODEV;
        return AF_PUSH_S_WAIT;
    }
    uint32_t oldest = 0, head = 0, epoch = 0;
    int ir = ops->info(ctx, kind, &oldest, &head, &epoch);
    if (ir == -1) return AF_PUSH_S_WAIT;
    if (ir != 0) return AF_PUSH_S_IDLE;
    uint32_t batch = r->eff_batch ? r->eff_batch : c->batch_rows;
    if (batch > c->batch_rows) batch = c->batch_rows;
    af_push_plan_t pl;
    af_push_plan(&r->cur, oldest, head, epoch, batch, &pl);
    if (pl.count == 0) return AF_PUSH_S_IDLE;
    af_push_pack_t pk;
    int pr = af_push_pack(ops->read, ctx, kind, pl.start, pl.count, mac, epoch, pl.lost, body, cap, &pk);
    if (pr == -3) {
        r->err = AF_PUSH_ERR_NODEV;
        return AF_PUSH_S_WAIT;
    }
    if (pr != 0) {
        r->err = AF_PUSH_ERR_NOSTORE;
        return AF_PUSH_S_WAIT;
    }
    if (pk.rows == 0) {
        if (pk.scan_end != pl.start) {
            af_push_advance_local(r, pk.scan_end, epoch);
            *cursor_moved = 1;
            return AF_PUSH_S_OK;
        }
        return AF_PUSH_S_WAIT;
    }
    char resp[160];
    resp[0] = 0;
    int http = 0;
    int post_res = ops->post(ctx, kind, mac, head, epoch, pk.first_seq, pk.lost, body, pk.len, &http, resp, sizeof resp);
    resp[sizeof resp - 1] = 0;
    uint32_t ts = ops->now_ts ? ops->now_ts(ctx) : 0;
    int act = af_push_on_result(r, auth_stop, http, post_res != 0, resp, strlen(resp), pk.first_seq, pk.rows, head, epoch, c->batch_rows, ts);
    if (act == AF_PUSH_A_OK) {
        *cursor_moved = 1;
        af_push_plan_t p2;
        af_push_plan(&r->cur, oldest, head, epoch, 0xFFFFFFFFu, &p2);
        *pending_left = p2.pending;
        return AF_PUSH_S_OK;
    }
    if (act == AF_PUSH_A_SHRINK || act == AF_PUSH_A_RETRY) return AF_PUSH_S_WAIT;
    if (act == AF_PUSH_A_STOP_AUTH || act == AF_PUSH_A_STOP_RING) return AF_PUSH_S_STOP;
    return AF_PUSH_S_WAIT;
}
