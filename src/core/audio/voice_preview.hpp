// =============================================================================
//  voice_preview.hpp — уривок голосу гравця для прослуховування в програмі.
//
//  Беремо кілька секунд мовлення з потрібного місця демо, з тією самою
//  обробкою, що піде у відео (вирівнювання гучності, шумодав), а довгі паузи
//  між фразами стискаємо — щоб за 15 секунд почути саме голос.
// =============================================================================
#pragma once

#include <cstdint>
#include <vector>

#include "voice_clean.hpp"

namespace gmdr::voice {
struct SpeakerTrack;
}

namespace gmdr::audio {

struct VoiceClip {
    std::vector<float> mono;             // 48 кГц
    int64_t            start = 0;        // семпл демо, з якого почато
    double             speech_seconds = 0;
};

// До max_speech секунд мовлення з першої фрази, що закінчується після from (семпли демо;
// якщо після from ніхто не говорив — з початку). fx — обробка (nullptr — сирий голос).
// Паузи довші за 0.3 с стискаються до 0.3 с.
VoiceClip make_voice_clip(const voice::SpeakerTrack& track, int64_t from, const VoiceCleanup* fx,
                          double max_speech = 15.0);

} // namespace gmdr::audio
