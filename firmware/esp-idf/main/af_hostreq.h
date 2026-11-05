#ifndef AF_HOSTREQ_H
#define AF_HOSTREQ_H
/* e16i b29 (V-17): усечённый/нечитаемый Host или Origin = отказ, а не "заголовка нет" (раньше TRUNC -> NULL -> пропуск).
   Коды возврата httpd передаются числами: без ESP-IDF, тест идёт на хосте. */
#include "af_hostchk.h"

enum { AF_HDR_PRESENT = 0, AF_HDR_ABSENT = 1, AF_HDR_BAD = 2 };

static inline int af_hdr_class(int rc, int ok_rc, int nf_rc)
{
    return rc == ok_rc ? AF_HDR_PRESENT : (rc == nf_rc ? AF_HDR_ABSENT : AF_HDR_BAD);
}

static inline int af_host_origin_req_ok(int hc, const char *host, int oc, const char *org)
{
    if (hc == AF_HDR_BAD || oc == AF_HDR_BAD) return 0;
    return af_host_origin_ok(hc == AF_HDR_PRESENT ? host : NULL, oc == AF_HDR_PRESENT ? org : NULL);
}
#endif
