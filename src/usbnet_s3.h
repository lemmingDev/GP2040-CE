#pragma once

// True iff both subnets are valid IPv4 /24 private-network addresses and differ.
// String-only (no IDF headers) so host tests compile this TU directly.
bool s3_validateSubnets(const char *apSubnet, const char *usbSubnet);
