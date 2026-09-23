#include <cassert>
#include <cstdio>
#include "usbnet_s3.h"
int main() {
    assert(s3_validateSubnets("192.168.4.0", "192.168.5.0") == true);   // defaults
    assert(s3_validateSubnets("192.168.4.0", "192.168.4.0") == false);  // must differ
    assert(s3_validateSubnets("192.168.4.0", "10.0.0.0") == true);      // second private range ok
    assert(s3_validateSubnets("8.8.8.0", "192.168.5.0") == false);      // non-private rejected
    assert(s3_validateSubnets("not-an-ip", "192.168.5.0") == false);    // garbage rejected
    assert(s3_validateSubnets("192.168.4.1", "192.168.5.0") == false);  // host bits set rejected
    esp_ip4_addr_t ip{};
    assert(s3_subnetToIp("192.168.5.0", &ip) == true);
    assert(ip.addr == 0xC0A80500u);  // network-byte-order integer
    assert(s3_subnetToIp("not-an-ip", &ip) == false);  // garbage rejected
    printf("usbnet validation PASS\n");
    return 0;
}
