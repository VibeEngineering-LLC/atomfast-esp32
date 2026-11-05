#ifndef AF_CSVNAME_H
#define AF_CSVNAME_H
#include <stdint.h>
#include <stddef.h>

/* Виды совпадают с AF_HQ_KIND_* в af_hist_query.h */
#define AF_CSVNAME_RAW 0
#define AF_CSVNAME_AGG 1
#define AF_CSVNAME_ENV 2

/* Записывает двухзначное число с ведущими нулями */
static inline void af_csv_p2(char *p, int v)
{
    p[0] = (char)('0' + v / 10);
    p[1] = (char)('0' + v % 10);
}

/* Записывает четырёхзначное число с ведущими нулями */
static inline void af_csv_p4(char *p, int v)
{
    p[0] = (char)('0' + v / 1000);
    p[1] = (char)('0' + (v / 100) % 10);
    p[2] = (char)('0' + (v / 10) % 10);
    p[3] = (char)('0' + v % 10);
}

/*
 * Пишет в buf имя "atomfast_<вид>_YYYYMMDD-HHMMSS.csv" (UTC) или,
 * при ts==0, "atomfast_<вид>_notime.csv".
 * Возврат: длина строки без NUL; -1 при ошибке.
 */
static inline int af_csv_name(uint8_t kind, uint32_t ts, char *buf, size_t cap)
{
    const char *nm;
    int len, pos, i;

    /* Имя вида; любое другое -> ошибка */
    if      (kind == AF_CSVNAME_RAW) nm = "raw";
    else if (kind == AF_CSVNAME_AGG) nm = "agg";
    else if (kind == AF_CSVNAME_ENV) nm = "env";
    else return -1;

    /* Счётчик символов «середины» между двумя подчёркиваниями */
    int mid = (ts == 0) ? 6 : 15;   /* "notime" | "YYYYMMDD-HHMMSS" */
    len = 9 + 3 + 1 + mid + 4;      /* "atomfast_" + nm + "_" + mid + ".csv" */

    /* Проверка ёмкости ДО любой записи */
    if (buf == NULL || cap < (size_t)len + 1u)
        return -1;

    /* "atomfast_" (9 символов) */
    {
        const char *pre = "atomfast_";
        for (i = 0; i < 9; i++) buf[i] = pre[i];
    }
    pos = 9;

    /* <вид> (3 символа) + '_' */
    buf[pos++] = nm[0];
    buf[pos++] = nm[1];
    buf[pos++] = nm[2];
    buf[pos++] = '_';

    if (ts == 0) {
        /* "notime" */
        const char *s = "notime";
        for (i = 0; i < 6; i++) buf[pos++] = s[i];
    } else {
        /* Разбор секунд эпохи на дату и время */
        uint32_t days = ts / 86400u;
        uint32_t rem  = ts % 86400u;
        int hh  = (int)(rem / 3600u);
        int mm  = (int)((rem % 3600u) / 60u);
        int ss  = (int)(rem % 60u);

        /* civil_from_days (Хауард Хиннант), знаковые 32-битные */
        int32_t z   = (int32_t)days + 719468;
        int32_t era = z / 146097;
        int32_t doe = z - era * 146097;
        int32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        int32_t y   = yoe + era * 400;
        int32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        int32_t mp  = (5 * doy + 2) / 153;
        int32_t d   = doy - (153 * mp + 2) / 5 + 1;
        int32_t mo  = (mp < 10) ? mp + 3 : mp - 9;
        if (mo <= 2) y++;

        /* YYYYMMDD-HHMMSS */
        af_csv_p4(buf + pos, (int)y);  pos += 4;
        af_csv_p2(buf + pos, (int)mo); pos += 2;
        af_csv_p2(buf + pos, (int)d);  pos += 2;
        buf[pos++] = '-';
        af_csv_p2(buf + pos, hh);     pos += 2;
        af_csv_p2(buf + pos, mm);     pos += 2;
        af_csv_p2(buf + pos, ss);     pos += 2;
    }

    /* ".csv" */
    buf[pos++] = '.';
    buf[pos++] = 'c';
    buf[pos++] = 's';
    buf[pos++] = 'v';
    buf[pos]   = '\0';

    return len;
}

#endif
