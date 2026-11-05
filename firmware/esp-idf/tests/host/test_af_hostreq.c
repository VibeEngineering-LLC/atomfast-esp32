#include "af_test.h"
#include "af_hostreq.h"
#define NF 0x105
#define TRUNC 0x1006 /* ESP_ERR_HTTPD_RESULT_TRUNC: важно лишь "не OK и не NOT_FOUND" */
#define P AF_HDR_PRESENT
#define A AF_HDR_ABSENT
#define B AF_HDR_BAD
#define H "192.0.2.131"
static void t_class(void)
{
    CHECK(af_hdr_class(0, 0, NF) == P && af_hdr_class(NF, 0, NF) == A);
    CHECK(af_hdr_class(TRUNC, 0, NF) == B && af_hdr_class(-1, 0, NF) == B);
}
static void t_req(void)
{
    CHECK(af_host_origin_req_ok(P, H, P, "http://" H) == 1);
    CHECK(af_host_origin_req_ok(P, H, A, "") == 1 && af_host_origin_req_ok(A, "", A, "") == 1);
    CHECK(af_host_origin_req_ok(P, H, B, "http://" H) == 0); /* V-17: усечённый Origin */
    CHECK(af_host_origin_req_ok(B, H, A, "") == 0);
    CHECK(af_host_origin_req_ok(P, "evil.com", A, "") == 0);
    CHECK(af_host_origin_req_ok(P, H, P, "http://evil.com") == 0);
}
int main(void) { RUN(t_class); RUN(t_req); TESTS_DONE(); }
