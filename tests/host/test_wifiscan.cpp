#include <cassert>
#include <cstdio>
#include <cstring>
#include "webconfig_scan.h"

static S3WifiNet mk(const char *s, int rssi, unsigned auth)
{
    S3WifiNet n;
    memset(&n, 0, sizeof(n));
    strncpy(n.ssid, s, sizeof(n.ssid) - 1);
    n.rssi = (int8_t)rssi;
    n.auth = (uint8_t)auth;
    return n;
}

int main()
{
    // Dedupe keeps strongest; empty SSIDs dropped; RSSI descending.
    S3WifiNet in[] = {
        mk("b", -80, 3), mk("", -40, 0), mk("a", -50, 3),
        mk("b", -60, 4), mk("c", -90, 0),
    };
    S3WifiNet out[8];
    size_t n = s3_build_scan_list(in, 5, out, 8);
    assert(n == 3);
    assert(strcmp(out[0].ssid, "a") == 0 && out[0].rssi == -50);
    assert(strcmp(out[1].ssid, "b") == 0 && out[1].rssi == -60 && out[1].auth == 4);
    assert(strcmp(out[2].ssid, "c") == 0);
    // Cap respected.
    S3WifiNet many[8];
    for (int i = 0; i < 8; i++)
    {
        char s[8];
        snprintf(s, sizeof(s), "n%d", i);
        many[i] = mk(s, -50 - i, 0);
    }
    assert(s3_build_scan_list(many, 8, out, 3) == 3);
    // Empty in, empty out.
    assert(s3_build_scan_list(in, 0, out, 8) == 0);
    printf("wifiscan: all assertions passed\n");
    return 0;
}
