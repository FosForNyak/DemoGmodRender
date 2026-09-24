// =============================================================================
//  frame_source.hpp — звідки конвеєр кодування бере кадри гри.
//
//  Кадри з рушія дає штатний startmovie: кожен кадр — окремий "файл"
//  <назва>0000.tga, <назва>0001.tga ... Два способи їх забрати:
//   * FrameSequenceReader (sequence_reader.hpp) — гра пише файли в тимчасову
//     папку, програма їх підхоплює, читає і видаляє;
//   * FramePipeReader (frame_pipe.hpp) — ті самі "файли" є каналами (іменовані
//     канали Windows, FIFO на Linux), і кадр іде з гри прямо в пам'ять програми,
//     без запису на диск.
//  Далі все однаково: кадри по порядку номерів, декодовані в Image.
// =============================================================================
#pragma once

#include <cstdint>
#include <string>

#include "image.hpp"

namespace gmdr::frames {

// Час, витрачений на кадри (для показу "що гальмує").
struct ReaderStats {
    int64_t frames = 0;
    double  read_ms = 0;     // сумарно: читання (файлів або каналу)
    double  decode_ms = 0;   // сумарно: декодування
};

class FrameSource {
public:
    virtual ~FrameSource() = default;

    // Гра завершила запис: більше нових кадрів не буде.
    virtual void set_producer_done() = 0;
    virtual bool producer_done() const = 0;

    // Наступний кадр за порядком, з обмеженням часу очікування.
    enum class Wait { Frame, Timeout, End };
    virtual Wait next_for(Image& out, int timeout_ms) = 0;

    virtual int64_t     pending_files() const = 0;   // кадрів отримано від гри, але ще не віддано
    virtual uint64_t    pending_bytes() const = 0;   // з них на диску (для каналу — 0)
    virtual int64_t     delivered() const = 0;
    virtual int64_t     skipped() const = 0;
    virtual bool        saw_any_file() const = 0;    // гра записала хоч один кадр туди, де ми чекаємо
    virtual std::string last_error() const = 0;
    virtual ReaderStats stats() const = 0;
};

} // namespace gmdr::frames
