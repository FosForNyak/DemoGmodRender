// =============================================================================
//  blender.hpp — motion blur (розмиття руху) змішуванням під-кадрів.
//
//  Гра рендерить, наприклад, 60 fps × 16 = 960 під-кадрів за секунду.
//  Ми усереднюємо кожні 16 під-кадрів в один вихідний кадр — виходить
//  "кінематографічне" розмиття як у справжньої камери.
//
//  Кут затвора (shutter): 360° — усереднюються всі під-кадри, 180° — перша
//  половина (класичний кіно-вигляд) тощо.
//
//  Обчислення паралельні: кадр ділиться на смуги рядків між ядрами CPU.
//  Результат можна отримати з 16 бітами на канал (BGR48 або YUV 16 біт): при
//  змішуванні багатьох кадрів точність зростає, це прибирає "смуги" (banding)
//  у 10-біт відео. Кадри JPEG змішуються прямо в YUV — перетворення кольору
//  лінійне, тож середнє в YUV дорівнює середньому в RGB.
// =============================================================================
#pragma once

#include <optional>
#include <vector>

#include "../util/thread_pool.hpp"
#include "image.hpp"

namespace gmdr::frames {

class MotionBlender {
public:
    // samples — під-кадрів на вихідний кадр (1 = без розмиття), до 256.
    MotionBlender(int samples, double shutter_degrees, bool high_depth_output, ThreadPool* pool);

    int samples() const { return samples_; }
    int used_samples() const { return used_; }

    // Додати під-кадр. Повертає готовий кадр, коли група заповнена.
    std::optional<Image> push(Image&& sub);
    // Незавершена група в кінці запису: повертає кадр із наявних під-кадрів (якщо є).
    std::optional<Image> flush();

private:
    Image finish_group();

    int                   samples_;
    int                   used_;
    bool                  high_depth_;
    ThreadPool*           pool_;
    int                   in_group_ = 0;
    int                   accumulated_ = 0;
    int                   width_ = 0, height_ = 0;
    PixelLayout           in_layout_ = PixelLayout::BGR24;
    bool                  full_range_ = true, bt709_ = false;
    int64_t               first_index_ = -1;
    // Суми по площинах: для BGR — одна площина з 3 каналами на піксель
    std::vector<uint16_t> acc_[3];
    int                   acc_width_[3] = {0, 0, 0};    // семплів у рядку суми
    int                   acc_height_[3] = {0, 0, 0};
};

} // namespace gmdr::frames
