// =============================================================================
//  overlay.hpp — підписи «хто говорить» прямо на кадрі відео.
//
//  Плашки з іменами праворуч унизу (як індикатор голосового чату в GMod): темна
//  напівпрозора смуга з кольоровою позначкою і ім'ям, плавно з'являється і зникає.
//  Малюється після motion blur, перед кодуванням — тож потрапляє в усі версії відео.
//  Шрифт — системний TTF з кирилицею (stb_truetype), кадри — будь-якого розміщення
//  (BGR, BGR 16 біт після motion blur, YUV кадрів JPEG).
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../frames/image.hpp"
#include "subtitles.hpp"

namespace gmdr::render {

class SpeakerOverlay {
public:
    struct Span {
        double a = 0, b = 0;   // секунди від початку відео
    };
    struct Speaker {
        std::string       name;
        std::vector<Span> spans;
    };

    // Відрізки мовлення для відео (як у субтитрах: близькі фрази злиті, короткі уривки відкинуто).
    static std::vector<Speaker> speakers_for(const std::vector<SpeakerSubtitleSource>& src, int64_t origin_sample,
                                             double duration, double delay, double speed = 1.0);
    // Системний шрифт з кирилицею; порожньо — не знайдено.
    static std::string find_font();

    // frame_h — висота кадрів (від неї розмір підписів). false — немає шрифту чи мовців.
    bool init(std::vector<Speaker> speakers, int frame_w, int frame_h, const std::string& font_path, std::string* error);
    // Намалювати на кадрі момент t (с від початку відео).
    void draw(frames::Image& img, double t) const;
    int  label_count() const { return static_cast<int>(labels_.size()); }

private:
    // Готова плашка: RGBA (не премультипліковані) у розмірі кадру
    struct Label {
        int                  w = 0, h = 0;
        std::vector<uint8_t> rgba;
    };
    std::vector<Speaker> speakers_;
    std::vector<Label>   labels_;
    int                  frame_w_ = 0, frame_h_ = 0;
    int                  margin_ = 0, gap_ = 0;
};

} // namespace gmdr::render
