# Vendored ArduinoJson (Task 3c)

Upstream: Benoit Blanchon `ArduinoJson` v6.21.2 (MIT, see LICENSE.txt) —
the EXACT version the Pico build pins via FetchContent (root
CMakeLists.txt:101-105), copied from the Pico FetchContent checkout
(`build/_deps/arduinojson-src`) because the WSL build machine has no
component-registry access (IDF component manager cannot fetch
`espressif/arduinojson`).

Functional files only (CMakeLists.txt, idf_component.yml, LICENSE.txt,
src/). Upstream already registers itself as an IDF component when
ESP_PLATFORM is set (`idf_component_register(INCLUDE_DIRS src)`), so no
edits were needed; S3 links it via `arduinojson` in REQUIRES
(esp32-s3/main/CMakeLists.txt). If the registry becomes reachable, prefer
a managed dependency (`espressif/arduinojson ^6.21.0`) and delete this
directory — but keep the version pinned to the Pico FetchContent tag.
