// S3 WiFi scan shaping helper (header-only, IDF-free so host tests can
// use it). Dedupe by SSID keeping strongest RSSI, drop empty SSIDs,
// sort RSSI descending, cap output. Pure logic; the endpoint in
// src/webconfig_s3.cpp converts wifi_ap_record_t into S3WifiNet first.
#pragma once

#include <stdint.h>
#include <string.h>

#define S3_WIFI_SCAN_MAX 20

struct S3WifiNet
{
    char ssid[33];
    int8_t rssi;
    uint8_t auth;
};

static size_t s3_build_scan_list(const S3WifiNet *in, size_t n_in,
    S3WifiNet *out, size_t n_cap)
{
    size_t n_out = 0;
    for (size_t i = 0; i < n_in; i++)
    {
        if (in[i].ssid[0] == '\0')
        {
            continue;
        }
        size_t k = 0;
        while (k < n_out && strcmp(out[k].ssid, in[i].ssid) != 0)
        {
            k++;
        }
        if (k < n_out)
        {
            if (in[i].rssi > out[k].rssi)
            {
                out[k].rssi = in[i].rssi;
                out[k].auth = in[i].auth;
            }
        }
        else if (n_out < n_cap)
        {
            strncpy(out[n_out].ssid, in[i].ssid, sizeof(out[n_out].ssid) - 1);
            out[n_out].ssid[sizeof(out[n_out].ssid) - 1] = '\0';
            out[n_out].rssi = in[i].rssi;
            out[n_out].auth = in[i].auth;
            n_out++;
        }
    }
    for (size_t a = 1; a < n_out; a++)
    {
        S3WifiNet t = out[a];
        size_t b = a;
        while (b > 0 && out[b - 1].rssi < t.rssi)
        {
            out[b] = out[b - 1];
            b--;
        }
        out[b] = t;
    }
    return n_out;
}
