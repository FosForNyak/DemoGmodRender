#include "voice_player.hpp"
#include "core/util/i18n.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#endif

namespace gmdr::gui {

constexpr int kRate = 48000;

#ifdef _WIN32
struct VoicePlayer::Impl {
    HWAVEOUT             wo = nullptr;
    WAVEHDR              hdr{};
    std::vector<int16_t> pcm;

    void close() {
        if (!wo) return;
        waveOutReset(wo);
        if (hdr.dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(wo, &hdr, sizeof(hdr));
        waveOutClose(wo);
        wo = nullptr;
        hdr = {};
    }
};

VoicePlayer::VoicePlayer() : impl_(std::make_unique<Impl>()) {}
VoicePlayer::~VoicePlayer() { impl_->close(); }
bool VoicePlayer::supported() { return true; }

bool VoicePlayer::play(const std::vector<float>& mono, std::string* error) {
    Impl& m = *impl_;
    m.close();
    if (mono.empty()) return false;
    m.pcm.resize(mono.size());
    for (size_t i = 0; i < mono.size(); ++i)
        m.pcm[i] = static_cast<int16_t>(std::lround(std::clamp(mono[i], -1.0f, 1.0f) * 32767.0f));
    WAVEFORMATEX wf{};
    wf.wFormatTag = WAVE_FORMAT_PCM;
    wf.nChannels = 1;
    wf.nSamplesPerSec = kRate;
    wf.wBitsPerSample = 16;
    wf.nBlockAlign = 2;
    wf.nAvgBytesPerSec = kRate * 2;
    if (waveOutOpen(&m.wo, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        m.wo = nullptr;
        if (error) *error = tr("не вдалося відкрити пристрій відтворення звуку");
        return false;
    }
    m.hdr.lpData = reinterpret_cast<LPSTR>(m.pcm.data());
    m.hdr.dwBufferLength = static_cast<DWORD>(m.pcm.size() * sizeof(int16_t));
    if (waveOutPrepareHeader(m.wo, &m.hdr, sizeof(m.hdr)) != MMSYSERR_NOERROR ||
        waveOutWrite(m.wo, &m.hdr, sizeof(m.hdr)) != MMSYSERR_NOERROR) {
        m.close();
        if (error) *error = tr("не вдалося відтворити звук");
        return false;
    }
    return true;
}

void VoicePlayer::stop() { impl_->close(); }

bool VoicePlayer::playing() const {
    const Impl& m = *impl_;
    return m.wo && (m.hdr.dwFlags & WHDR_PREPARED) && !(m.hdr.dwFlags & WHDR_DONE);
}

double VoicePlayer::position() const {
    const Impl& m = *impl_;
    if (!m.wo) return 0;
    MMTIME t{};
    t.wType = TIME_SAMPLES;
    if (waveOutGetPosition(m.wo, &t, sizeof(t)) != MMSYSERR_NOERROR || t.wType != TIME_SAMPLES) return 0;
    return static_cast<double>(t.u.sample) / kRate;
}

double VoicePlayer::duration() const { return static_cast<double>(impl_->pcm.size()) / kRate; }

#else
struct VoicePlayer::Impl {};
VoicePlayer::VoicePlayer() : impl_(std::make_unique<Impl>()) {}
VoicePlayer::~VoicePlayer() = default;
bool VoicePlayer::supported() { return false; }
bool VoicePlayer::play(const std::vector<float>&, std::string* error) {
    if (error) *error = tr("прослуховування поки є лише у версії для Windows");
    return false;
}
void VoicePlayer::stop() {}
bool VoicePlayer::playing() const { return false; }
double VoicePlayer::position() const { return 0; }
double VoicePlayer::duration() const { return 0; }
#endif

} // namespace gmdr::gui
