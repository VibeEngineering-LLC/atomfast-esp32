#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "../../main/ble_addr.h"

static int g_fail = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL: %s line %d\n", #cond, __LINE__); \
        g_fail++; \
    } \
} while (0)

static const uint8_t sentinel[6] = {0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A};

int main(void)
{
    uint8_t out[6];
    static const uint8_t exp[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

    /* --- 1. valid formats --- */
    CHECK(ble_addr_parse("AA:BB:CC:DD:EE:FF", out) && memcmp(out, exp, 6) == 0);
    CHECK(ble_addr_parse("aa:bb:cc:dd:ee:ff", out) && memcmp(out, exp, 6) == 0);
    CHECK(ble_addr_parse("AA-BB-CC-DD-EE-FF", out) && memcmp(out, exp, 6) == 0);
    CHECK(ble_addr_parse("AABBCCDDEEFF", out) && memcmp(out, exp, 6) == 0);
    CHECK(ble_addr_parse("aabbccddeeff", out) && memcmp(out, exp, 6) == 0);
    CHECK(ble_addr_parse("Aa:bB:Cc:dD:eE:fF", out) && memcmp(out, exp, 6) == 0);

    /* --- 2. byte order --- */
    static const uint8_t ord[6] = {1, 2, 3, 4, 5, 6};
    CHECK(ble_addr_parse("01:02:03:04:05:06", out) && memcmp(out, ord, 6) == 0);

    /* --- 3. rejections (false + out unchanged) --- */

    memset(out, 0x5A, 6);
    CHECK(!ble_addr_parse("AA:BB:CC:DD:EE", out) && memcmp(out, sentinel, 6) == 0);

    memset(out, 0x5A, 6);
    CHECK(!ble_addr_parse("AA:BB:CC:DD:EE:FF:00", out) && memcmp(out, sentinel, 6) == 0);

    memset(out, 0x5A, 6);
    CHECK(!ble_addr_parse("AA:BB:CC:DD:EE:GG", out) && memcmp(out, sentinel, 6) == 0);
    CHECK(!ble_addr_parse("aa:bb:cc:dd:ee:gg", out) && memcmp(out, sentinel, 6) == 0);
    CHECK(!ble_addr_parse("aabbccddeegg", out) && memcmp(out, sentinel, 6) == 0);

    memset(out, 0x5A, 6);
    CHECK(!ble_addr_parse(" AA:BB:CC:DD:EE:FF", out) && memcmp(out, sentinel, 6) == 0);

    memset(out, 0x5A, 6);
    CHECK(!ble_addr_parse("AA:BB:CC:DD:EE:FF ", out) && memcmp(out, sentinel, 6) == 0);

    memset(out, 0x5A, 6);
    CHECK(!ble_addr_parse("AA:BB:CC:DD: E:FF", out) && memcmp(out, sentinel, 6) == 0);

    memset(out, 0x5A, 6);
    CHECK(!ble_addr_parse("AA:BB-CC:DD:EE:FF", out) && memcmp(out, sentinel, 6) == 0);

    memset(out, 0x5A, 6);
    CHECK(!ble_addr_parse("", out) && memcmp(out, sentinel, 6) == 0);

    memset(out, 0x5A, 6);
    CHECK(!ble_addr_parse("AABBCCDDEEF", out) && memcmp(out, sentinel, 6) == 0);

    memset(out, 0x5A, 6);
    CHECK(!ble_addr_parse("AABBCCDDEEFFF", out) && memcmp(out, sentinel, 6) == 0);

    memset(out, 0x5A, 6);
    CHECK(!ble_addr_parse("AA:BB:CC:DD:EE:FF:", out) && memcmp(out, sentinel, 6) == 0);

    memset(out, 0x5A, 6);
    CHECK(!ble_addr_parse("A:BB:CC:DD:EE:FF", out) && memcmp(out, sentinel, 6) == 0);

    /* --- 4. NULL arguments --- */
    CHECK(!ble_addr_parse(NULL, out));
    CHECK(!ble_addr_parse("AA:BB:CC:DD:EE:FF", NULL));

    /* --- 5. no '\0' within first 18 chars --- */
    char buf[41];
    memset(buf, 'A', 40);
    buf[40] = '\0';
    memset(out, 0x5A, 6);
    CHECK(!ble_addr_parse(buf, out));

    if (g_fail) {
        printf("test_ble_addr: FAIL\n");
        return 1;
    }
    printf("test_ble_addr: OK\n");
    return 0;
}
