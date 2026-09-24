// =============================================================================
//  frame_transport.hpp — як кадри йдуть з гри в програму.
//
//  Типово — каналом (frames/frame_pipe.hpp): кадр з startmovie потрапляє з гри
//  прямо в пам'ять програми, на диск не пишеться нічого. Якщо рушій у канал не
//  пише (інша збірка гри по-іншому обробляє назву файлу), рендер сам переходить
//  на файли в тимчасовій папці, а програма запам'ятовує це для цієї гри
//  (<дані програми>/frame_transport.json): наступні рендери одразу пишуть файли —
//  30 днів, до оновлення гри (змінився її exe) або доки програма не почне
//  пропонувати гри канал іншою назвою.
// =============================================================================
#pragma once

#include <filesystem>
#include <string>

#include "settings.hpp"

namespace gmdr::render {

struct TransportChoice {
    bool        pipe = false;     // каналом (інакше файлами)
    bool        strict = false;   // лише каналом, без переходу на файли (налаштування "pipe")
    std::string note;             // чому файли, хоч типово канал (для журналу)
};

// "auto" / "pipe" / "files" (щось інше — як "auto").
std::string normalize_frame_transport(const std::string& value);

TransportChoice choose_frame_transport(const RenderSettings& s, const std::filesystem::path& game_exe);

// Канал з цією грою не спрацював — наступні рендери одразу файлами.
void remember_pipe_failure(const std::filesystem::path& game_exe, const std::string& why);
// Канал з цією грою працює (після явного "pipe") — забути попередню невдачу.
void forget_pipe_failure(const std::filesystem::path& game_exe);

} // namespace gmdr::render
