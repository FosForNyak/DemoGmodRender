// =============================================================================
//  capabilities.hpp — що вміє цей комп'ютер: процесор, пам'ять, відеокарти,
//  кодеки FFmpeg (і які GPU-кодеки справді відкриваються), копії гри для кожного
//  рендерера, розпізнавання мовлення, рушій озвучення, ключі сервісів, місце на
//  дисках і графічні API самого вікна.
//
//  Це стан середовища, а не налаштування і не рішення про сумісність: що з чим
//  можна поєднати, вирішує constraints.hpp, отримавши налаштування і цей опис.
//  Кожне значення має стан: є / немає / невідомо (не перевірялося) / не вдалося /
//  не стосується / перевіряється. Те, що не перевірено, — «невідомо», а не «є».
//
//  scan_environment() — швидкі перевірки (файли, списки FFmpeg, пам'ять);
//  проба GPU-кодеків відкриттям (probe_video_encoder) повільна, тож її роблять
//  окремо, у фоні, і дописують результат (set_encoder_probe).
// =============================================================================
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "../render/settings.hpp"

namespace gmdr::config {

enum class Availability { Unknown, Available, Unavailable, Failed, NotApplicable, Checking };
const char* availability_id(Availability a);   // "unknown", "available" ... (для QML і журналу)

struct CapabilityState {
    Availability state = Availability::Unknown;
    std::string  detail;   // шлях, версія або чому ні
    bool ok() const { return state == Availability::Available; }
};

struct GpuInfo {
    std::string name;
    std::string vendor;          // nvidia, amd, intel, microsoft, інше
    uint32_t    vendor_id = 0;
    uint64_t    vram_bytes = 0;  // 0 — невідомо
    std::string driver;          // версія драйвера (де ОС її дає)
    bool        software = false;   // програмний адаптер (Microsoft Basic Render Driver)
};

struct HardwareCapabilities {
    unsigned             cpu_threads = 0;
    uint64_t             ram_bytes = 0;   // 0 — невідомо
    std::vector<GpuInfo> gpus;
    CapabilityState      gpu_info;        // чи вдалося прочитати список відеокарт
};

// Графічний API вікна програми (Qt). Його заповнює інтерфейс — ядро цього не знає.
struct GraphicsApiInfo {
    std::string  id;       // auto, d3d11, d3d12, vulkan, opengl, metal, software
    std::string  label;
    Availability state = Availability::Unknown;   // Available — ініціалізується; Unavailable — ні
    std::string  detail;
};
struct GraphicsCapabilities {
    std::vector<GraphicsApiInfo> apis;
    std::string                  active;   // який API вікно використовує зараз
    std::string                  adapter;  // відеокарта, на якій малює вікно (якщо відомо)
};

struct EncoderState {
    CapabilityState state;   // Available — є у збірці FFmpeg (і, для GPU, проба відкриття пройшла)
    bool            compiled = false;
    bool            gpu = false;
    bool            probed = false;   // GPU: пробу вже зроблено
};
struct EncoderCapabilities {
    std::map<std::string, EncoderState>    video;   // ключ — енкодер FFmpeg
    std::map<std::string, CapabilityState> audio;
    bool                                   gpu_probe_done = false;
};

struct MediaCapabilities {
    std::string                            ffmpeg_version;   // libavcodec x.y.z
    std::map<std::string, CapabilityState> filters;          // thumbnail, palettegen, arnndn, loudnorm ...
    std::map<std::string, CapabilityState> extra_encoders;   // gif, libwebp, mjpeg — для похідних файлів
    CapabilityState                        rnnoise_model;    // модель шумодава поруч із програмою
};

struct GameInstallInfo {
    std::string     renderer;   // id рендерера
    CapabilityState install;    // detail — папка гри або що зробити
    bool            from_settings = false;   // папку вказано в налаштуваннях (а не знайдено автоматично)
    bool            has_64bit = false;
    bool            accepted = false;        // рендерер визнає копію своєю (RTX: є Remix)
    std::string     driver;                  // installed / outdated / missing
};
struct GameCapabilities {
    std::vector<GameInstallInfo> installs;       // по одному на рендерер
    CapabilityState              running;        // Available — гра вже запущена (рендеру це заважає)
    CapabilityState              frame_pipes;    // передача кадрів каналом
    const GameInstallInfo* find(const std::string& renderer) const;
};

struct SpeechCapabilities {
    CapabilityState whisper_cli;
    CapabilityState model;
};

struct TranslationCapabilities {
    std::map<std::string, CapabilityState> providers;   // налаштовано: є ключ / адреса
};

struct DubbingCapabilities {
    CapabilityState omnivoice;    // локальний рушій встановлено (є Python з omnivoice)
    CapabilityState elevenlabs;   // є ключ
    CapabilityState nvidia_gpu;   // для OmniVoice на CUDA
};

struct FilesystemCapabilities {
    CapabilityState demo;          // файл демо є
    CapabilityState mic;           // файл мікрофона є (NotApplicable — не вибрано)
    CapabilityState output_exists; // вихідний файл уже є (перезапис)
    uint64_t        output_free = 0;   // вільно на диску результату (0 — невідомо)
    uint64_t        game_free = 0;     // вільно на диску з грою
};

struct PlatformInfo {
    std::string os;        // windows, linux, macos
    std::string os_label;  // людська назва з версією
};

struct EnvironmentCapabilities {
    PlatformInfo            platform;
    HardwareCapabilities    hardware;
    GraphicsCapabilities    graphics;
    EncoderCapabilities     encoders;
    MediaCapabilities       media;
    GameCapabilities        game;
    SpeechCapabilities      speech;
    TranslationCapabilities translation;
    DubbingCapabilities     dubbing;
    FilesystemCapabilities  filesystem;

    // Стан відеокодера (Unavailable — немає у збірці; Unknown — GPU ще не перевірено)
    CapabilityState video_encoder(const std::string& name) const;
    CapabilityState audio_encoder(const std::string& name) const;
};

struct ScanOptions {
    bool hardware = true;      // CPU, RAM, відеокарти
    bool game = true;          // копії гри, запущена гра
    bool processes = true;     // чи запущена гра (перелік процесів)
    bool all_renderers = true; // копії гри для всіх рендерерів (false — лише вибраного: перед рендером)
};

// Швидкі перевірки. Шляхи з налаштувань (папки гри, whisper, мікрофон, вихід) — з s.
EnvironmentCapabilities scan_environment(const render::RenderSettings& s, const ScanOptions& opt = {});
// Лише те, що залежить від шляхів у налаштуваннях (після зміни папки, файлу мікрофона...)
void rescan_paths(EnvironmentCapabilities& env, const render::RenderSettings& s);

// Проба GPU-кодека: відкрити його (1280×720). Повільно (сотні мс) — не в потоці інтерфейсу.
CapabilityState probe_video_encoder(const std::string& name);
// GPU-кодеки, які є у збірці FFmpeg і ще не пробувались
std::vector<std::string> gpu_encoders_to_probe(const EnvironmentCapabilities& env);
void set_encoder_probe(EnvironmentCapabilities& env, const std::string& name, const CapabilityState& result);

// Найкращий робочий GPU-кодек сімейства (hevc → hevc_nvenc/amf/qsv); порожньо — немає
std::string best_gpu_encoder(const EnvironmentCapabilities& env, const std::string& family);

} // namespace gmdr::config
