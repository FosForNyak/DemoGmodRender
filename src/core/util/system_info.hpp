// =============================================================================
//  system_info.hpp — ОС, процесор, пам'ять і відеокарти цього комп'ютера.
//
//  Лише те, що ОС дає перевірити: чого не вдалося прочитати, лишається порожнім
//  (0 — невідомо), а не вигаданим. Windows: RtlGetVersion, реєстр, DXGI
//  (назва, пам'ять і драйвер відеокарт). Linux: uname, /proc, /sys/class/drm.
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gmdr {

struct SystemGpu {
    std::string name;
    uint32_t    vendor_id = 0;       // PCI: 0x10DE NVIDIA, 0x1002 AMD, 0x8086 Intel
    uint64_t    vram_bytes = 0;      // 0 — невідомо
    std::string driver;              // версія драйвера (Windows) або модуль ядра (Linux)
    bool        software = false;    // програмний адаптер (Microsoft Basic Render Driver)
};

std::string            os_id();            // windows / linux / macos
std::string            os_description();   // "Windows 11 10.0 (збірка 22631)"
std::string            cpu_name();         // "?" — невідомо
uint64_t               total_memory();     // байтів; 0 — невідомо
uint64_t               available_memory(); // байтів; 0 — невідомо
// Відеокарти; ok=false — список прочитати не вдалося (тоді він порожній, а не "немає відеокарт")
std::vector<SystemGpu> system_gpus(bool* ok = nullptr);
std::string            gpu_vendor_name(uint32_t vendor_id);   // nvidia / amd / intel / microsoft / ""

} // namespace gmdr
