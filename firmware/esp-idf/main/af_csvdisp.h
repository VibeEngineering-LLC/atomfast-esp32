#ifndef AF_CSVDISP_H
#define AF_CSVDISP_H
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include "af_csvname.h"
/* e16i П.4: значение заголовка Content-Disposition для выгрузки CSV. cd живёт до конца ответа
 * (httpd_resp_set_hdr хранит указатель, не копию); cap >= 64. Время UTC; нет часов -> "notime". */
static inline void af_csv_disp_fill(char *cd, size_t cap, uint8_t kind) {
    time_t t = time(NULL);
    uint32_t ts = (t < 1700000000) ? 0u : (uint32_t)t;
    char nm[40];
    if (af_csv_name(kind, ts, nm, sizeof nm) > 0) snprintf(cd, cap, "attachment; filename=\"%s\"", nm);
    else cd[0] = '\0';
}
#endif
