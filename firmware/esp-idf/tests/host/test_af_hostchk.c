#include "af_test.h"
#include "af_hostchk.h"

static void t_host_name_ok(void)
{
    CHECK(af_host_name_ok("192.0.2.131") == 1);
    CHECK(af_host_name_ok("192.0.2.131:80") == 1);
    CHECK(af_host_name_ok("atomfast") == 1);
    CHECK(af_host_name_ok("atomfast.local") == 1);
    CHECK(af_host_name_ok("atomfast.local:80") == 1);
    CHECK(af_host_name_ok("localhost") == 1);
    CHECK(af_host_name_ok("[::1]:80") == 1);
    CHECK(af_host_name_ok(NULL) == 1);
    CHECK(af_host_name_ok("") == 1);
    CHECK(af_host_name_ok("evil.com") == 0);
    CHECK(af_host_name_ok("rebind.attacker.example:80") == 0);
    CHECK(af_host_name_ok("192.0.2.131.evil.com") == 0);
    CHECK(af_host_name_ok("x.local.evil.com") == 0);
    CHECK(af_host_name_ok(".local") == 0);
    CHECK(af_host_name_ok(":80") == 0);
}

static void t_host_origin_ok(void)
{
    CHECK(af_host_origin_ok("192.0.2.131", NULL) == 1);
    CHECK(af_host_origin_ok("192.0.2.131", "http://192.0.2.131") == 1);
    CHECK(af_host_origin_ok("192.0.2.131:80", "http://192.0.2.131:80") == 1);
    CHECK(af_host_origin_ok("192.0.2.131", "http://evil.com") == 0);
    CHECK(af_host_origin_ok("192.0.2.131", "null") == 0);
    CHECK(af_host_origin_ok("192.0.2.131", "https://192.0.2.131") == 0);
    CHECK(af_host_origin_ok("evil.com", "http://evil.com") == 0);
    CHECK(af_host_origin_ok("192.0.2.131", "http://192.0.2.1310") == 0);
}

static void t_extra(void)
{
    CHECK(af_host_name_ok("a.xlocal") == 0);
    CHECK(af_host_origin_ok("192.0.2.131", "ftp://-192.0.2.131") == 0);
}

int main(void)
{
    RUN(t_host_name_ok);
    RUN(t_host_origin_ok);
    RUN(t_extra);
    TESTS_DONE();
}
