// =============================================================================
//  derived.hpp — похідні файли з готового відео: обкладинка і анімації.
//
//  Робляться після рендеру з основного файлу (декодування + фільтри FFmpeg):
//   * обкладинка JPG — фільтр thumbnail вибирає найвиразніший кадр біля середини;
//   * GIF — палітра під саме це відео (palettegen/paletteuse), кольори не "бруднішають";
//   * WebP — анімація, у кілька разів менша за GIF при кращій якості.
// =============================================================================
#pragma once

#include <string>

namespace gmdr::render {

// Обкладинка: кадр біля at_seconds (< 0 — середина відео), вписаний у max_w×max_h.
bool make_thumbnail(const std::string& video, const std::string& out_jpg, double at_seconds, int max_w, int max_h,
                    std::string* error);

enum class AnimFormat { Gif, WebP };
// Анімація з початку відео: не довша за max_seconds, ширина width, fps кадрів/с.
bool make_animation(const std::string& video, const std::string& out_path, AnimFormat format, double max_seconds,
                    int width, int fps, std::string* error);

} // namespace gmdr::render
