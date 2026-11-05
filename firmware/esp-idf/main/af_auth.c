#include "af_net.h"
#include "esp_http_server.h"
#include <stdbool.h>

/* e16g (T-48, решение оператора 28.10): веб и API AtomFast без пароля, как у AtomSpectra-водопада.
   Пароль в образ не зашивается. CSRF для мутирующих POST остаётся (web_server.c). */

void af_auth_init(void) {}

bool af_auth_check(httpd_req_t *req) {
    (void)req;
    return true;
}
