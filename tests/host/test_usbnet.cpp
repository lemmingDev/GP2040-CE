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
    printf("usbnet validation PASS\n");
    return 0;
}
