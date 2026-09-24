// =============================================================================
//  formats.hpp — що програма вміє записувати: контейнери, відео- й аудіокодеки,
//  типові роздільні здатності й частоти кадрів.
//
//  Це опис можливостей формату, а не стан комп'ютера: чи є кодек у збірці FFmpeg
//  і чи працює GPU-кодек на цій відеокарті — у capabilities.hpp, а що з чим
//  сумісне в конкретних налаштуваннях — у constraints.hpp. Інтерфейс бере списки
//  звідси (через стани налаштувань), а не тримає свої.
// =============================================================================
#pragma once

#include <string>
#include <vector>

#include "../render/settings.hpp"

namespace gmdr::config {

struct ContainerInfo {
    std::string ext;           // розширення: mp4, mkv ... (для послідовностей кадрів — png, tiff ...)
    std::string label;         // український ключ перекладу
    std::string image_codec;   // послідовність кадрів: єдиний кодек (png, tiff, bmp, mjpeg); порожньо — відео
    bool        chapters = false;    // розділи з позначок
    bool        joinable = false;    // частини склеюються без перекодування (паралельний рендер, дописування)
    bool        mov_family = false;  // MP4/MOV: faststart, запис фрагментами
};
const std::vector<ContainerInfo>& containers();
const ContainerInfo*              find_container(const std::string& ext);
// Контейнер налаштувань: явний (container) або за розширенням вихідного файлу; типово mp4
std::string container_of(const render::RenderSettings& s);
bool        is_image_sequence(const render::RenderSettings& s);
// Шлях з іншим розширенням (для виправлень «формат MKV»); порожній шлях лишається порожнім
std::string replace_ext(const std::string& path, const std::string& ext);

struct VideoEncoderInfo {
    std::string name;      // енкодер FFmpeg
    std::string label;     // український ключ перекладу
    std::string family;    // h264, hevc, av1, vp9, prores, dnxhd, ffv1, utvideo, png ...
    std::string vendor;    // GPU: nvidia, amd, intel, vulkan, d3d12, vaapi; CPU — порожньо
    bool        gpu = false;
};
const std::vector<VideoEncoderInfo>& video_encoders();
const VideoEncoderInfo*              find_video_encoder(const std::string& name);
// Такий самий формат на процесорі (h264_nvenc → libx264); порожньо — немає
std::string cpu_equivalent(const std::string& encoder);

struct AudioEncoderInfo {
    std::string name;
    std::string label;
    bool        lossless = false;
};
const std::vector<AudioEncoderInfo>& audio_encoders();
const AudioEncoderInfo*              find_audio_encoder(const std::string& name);
bool                                 audio_lossless(const std::string& codec);

struct ResolutionInfo {
    std::string label;   // український ключ перекладу
    int         w = 0, h = 0;
};
const std::vector<ResolutionInfo>& resolutions();
const std::vector<std::string>&    frame_rates();       // 24, 25, 30 ... 59.94
const std::vector<double>&         speeds();            // 0.25 ... 8
const std::vector<double>&         loudness_targets();  // 0 (не змінювати), -14, -16, -23

} // namespace gmdr::config
