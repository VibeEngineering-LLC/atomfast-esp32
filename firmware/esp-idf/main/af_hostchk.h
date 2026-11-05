#ifndef AF_HOSTCHK_H
#define AF_HOSTCHK_H

#include <stddef.h>
#include <string.h>

static inline int af_host_name_ok(const char *host)
{
    if (host == NULL || host[0] == '\0')
        return 1;
    if (host[0] == '[')
        return 1;

    size_t n = 0;
    while (host[n] != '\0' && host[n] != ':')
        n++;

    if (n == 0)
        return 0;

    int has_dot = 0;
    for (size_t i = 0; i < n; i++) {
        if (host[i] == '.') {
            has_dot = 1;
            break;
        }
    }
    if (!has_dot)
        return 1;

    int digits_dots = 1;
    for (size_t i = 0; i < n; i++) {
        if ((host[i] < '0' || host[i] > '9') && host[i] != '.') {
            digits_dots = 0;
            break;
        }
    }
    if (digits_dots)
        return 1;

    if (n > 6 && n >= 6 &&
        host[n - 6] == '.' && host[n - 5] == 'l' && host[n - 4] == 'o' &&
        host[n - 3] == 'c' && host[n - 2] == 'a' && host[n - 1] == 'l')
        return 1;

    return 0;
}

static inline int af_host_origin_ok(const char *host, const char *origin)
{
    if (!af_host_name_ok(host))
        return 0;
    if (origin == NULL || origin[0] == '\0')
        return 1;
    if (strncmp(origin, "http://", 7) != 0)
        return 0;
    if (host == NULL)
        return 0;
    return strcmp(origin + 7, host) == 0;
}

#endif /* AF_HOSTCHK_H */
