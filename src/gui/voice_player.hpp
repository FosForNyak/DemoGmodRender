// =============================================================================
//  voice_player.hpp — відтворення короткого уривку звуку (прослуховування голосу).
//  Windows — waveOut (вбудований у систему, без додаткових бібліотек).
// =============================================================================
#pragma once

#include <memory>
#include <string>
#include <vector>

namespace gmdr::gui {

class VoicePlayer {
public:
    VoicePlayer();
    ~VoicePlayer();
    VoicePlayer(const VoicePlayer&) = delete;
    VoicePlayer& operator=(const VoicePlayer&) = delete;

    static bool supported();
    // Відтворити моно 48 кГц (попереднє зупиняється).
    bool play(const std::vector<float>& mono, std::string* error = nullptr);
    void stop();
    bool playing() const;
    double position() const;   // с від початку уривка
    double duration() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace gmdr::gui
