# Vendored led_strip (Task 5)

Upstream: Espressif `espressif/led_strip` v2.5.2 (Apache-2.0, see LICENSE),
copied verbatim from `lib/tinyusb/hw/bsp/espressif/components/led_strip`
(the tinyusb submodule's copy of the same component) because the WSL build
machine has no component-registry access.

Functional files only (CMakeLists.txt, idf_component.yml, LICENSE,
include/, interface/, src/). Its `REQUIRES driver` resolves against IDF
v5.4's `driver` component with no edits. If the registry becomes reachable,
prefer a managed dependency and delete this directory.
