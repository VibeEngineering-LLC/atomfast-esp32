#ifndef AF_FMTVAL_H
#define AF_FMTVAL_H
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* Округление "половина вверх": floor(x*10^d+0.5)/10^d (d>=0), floor(x/10^k+0.5)*10^k (d<0) */
static inline double af_round_to(double x, int dec) {
    if (dec >= 0) {
        double p = pow(10.0, (double)dec);
        return floor(x * p + 0.5) / p;
    } else {
        double p = pow(10.0, (double)(-dec));
        return floor(x / p + 0.5) * p;
    }
}

/* Число знаков после запятой: 1-2 значащие цифры, clamp [-12,12] */
static inline int af_dec_for(double abs) {
    if (abs <= 0.0) return 0;
    double e = floor(log10(abs));
    int first = (int)floor(abs / pow(10.0, e) + 1e-12);
    int sig = (first == 1 || first == 2) ? 2 : 1;
    int dec = sig - 1 - (int)e;
    if (dec < -12) dec = -12;
    if (dec > 12) dec = 12;
    return dec;
}

/* Печать в экспоненциальной форме, приведение экспоненты (без +, без ведущих нулей) */
static inline void af_fmt_exp(char *buf, size_t cap, double val, int md) {
    if (cap == 0) return;
    char tmp[80];
    snprintf(tmp, sizeof(tmp), "%.*e", md, val);
    /* убрать '+' и ведущие нули в экспоненте */
    char *ep = strchr(tmp, 'e');
    if (ep) {
        ep++;
        if (*ep == '+') {
            memmove(ep, ep + 1, strlen(ep + 1) + 1);
        } else if (*ep == '-') {
            char *z = ep + 1;
            while (*z == '0') z++;
            if (z != ep + 1) memmove(ep + 1, z, strlen(z) + 1);
        } else {
            char *z = ep;
            while (*z == '0') z++;
            if (z != ep) memmove(ep, z, strlen(z) + 1);
        }
    }
    size_t n = strlen(tmp);
    if (n >= cap) n = cap - 1;
    memcpy(buf, tmp, n);
    buf[n] = '\0';
}

/* Значение (мкЗв/ч) и относительная погрешность (%) -> три строки для CSV.
   v_out - значение, s_out - абсолютная погрешность (1-2 значащие цифры), e_out - погрешность в %. */
static inline void af_fmt_rate(char *v_out, size_t vcap,
                               char *s_out, size_t scap,
                               char *e_out, size_t ecap,
                               double value, double eps_pct) {
    if (vcap > 0) v_out[0] = '\0';
    if (scap > 0) s_out[0] = '\0';
    if (ecap > 0) e_out[0] = '\0';

    /* нет значения */
    if (!isfinite(value)) return;

    /* нет погрешности */
    if (!isfinite(eps_pct) || eps_pct < 0.0) {
        if (vcap > 0) snprintf(v_out, vcap, "%.3g", value);
        return;
    }

    /* e_out = eps_pct с одной цифрой */
    if (ecap > 0) snprintf(e_out, ecap, "%.1f", eps_pct);

    /* value == 0 */
    if (value == 0.0) {
        if (vcap > 0) snprintf(v_out, vcap, "0");
        if (scap > 0) snprintf(s_out, scap, "0");
        return;
    }

    /* eps_pct == 0 */
    if (eps_pct == 0.0) {
        if (vcap > 0) snprintf(v_out, vcap, "%.3g", value);
        if (scap > 0) snprintf(s_out, scap, "0");
        return;
    }

    /* abs = |value| * eps / 100 */
    double abs = fabs(value) * eps_pct / 100.0;

    int dec = af_dec_for(abs);
    double rounded = af_round_to(abs, dec);
    if (rounded > 0.0) dec = af_dec_for(rounded);

    int exp_form = (fabs(value) < 1e-4 || fabs(value) >= 1e9);

    if (exp_form) {
        /* значение: экспоненциальная */
        if (vcap > 0) {
            int e_val = (int)floor(log10(fabs(value)));
            int e_abs = (int)floor(log10(abs));
            double e10 = pow(10.0, (double)e_abs);
            int first = (int)floor(abs / e10 + 1e-12);
            int sig = (first == 1 || first == 2) ? 2 : 1;
            int md = e_val - e_abs + (sig - 1);
            if (md < 0) md = 0;
            af_fmt_exp(v_out, vcap, value, md);
        }
        /* s_out: экспоненциальная */
        if (scap > 0) {
            if (rounded <= 0.0) {
                snprintf(s_out, scap, "0");
            } else {
                int e_s = (int)floor(log10(fabs(rounded)));
                int e_abs = (int)floor(log10(abs));
                double e10 = pow(10.0, (double)e_abs);
                int first = (int)floor(abs / e10 + 1e-12);
                int sig = (first == 1 || first == 2) ? 2 : 1;
                int md = e_s - e_abs + (sig - 1);
                if (md < 0) md = 0;
                af_fmt_exp(s_out, scap, rounded, md);
            }
        }
    } else if (dec >= 0) {
        /* значение: обычная, dec знаков */
        if (vcap > 0) {
            double rv = af_round_to(value, dec);
            snprintf(v_out, vcap, "%.*f", dec, rv);
        }
        /* s_out: обычная, dec знаков */
        if (scap > 0) {
            snprintf(s_out, scap, "%.*f", dec, rounded);
        }
    } else {
        /* значение: целое */
        if (vcap > 0) {
            long long rv = (long long)af_round_to(value, dec);
            snprintf(v_out, vcap, "%lld", rv);
        }
        /* s_out: целое */
        if (scap > 0) {
            long long rs = (long long)rounded;
            snprintf(s_out, scap, "%lld", rs);
        }
    }
}

#endif /* AF_FMTVAL_H */
