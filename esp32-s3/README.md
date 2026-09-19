# ESP32-S3 build (Phase 0 PoC)

1. Install ESP-IDF v5.x and export it (`export IDF_PATH=...`, `idf.py --version`).
2. Configure: `idf.py -DGP2040_BOARDCONFIG=ESP32S3DevKitC1 set-target esp32s3 reconfigure`
3. Build: `idf.py build`
4. Flash + monitor: `idf.py -p <PORT> flash monitor` (use the USB-Serial/JTAG port; hold BOOT + tap RESET if needed).
