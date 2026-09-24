// =============================================================================
//  dependencies.hpp — зовнішні складники, від яких залежать функції програми:
//  копії гри для кожного рендерера, whisper-cli і модель розпізнавання, рушій
//  озвучення, ключі сервісів, модель шумодава, FFmpeg.
//
//  Один перелік для «Налаштування → Система», звіту про проблему і CLI
//  (--diagnostics): що це, для чого, чи є, чи потрібне саме зараз (за
//  налаштуваннями) і що зробити. Стан береться з EnvironmentCapabilities —
//  тут нічого не перевіряється вдруге.
// =============================================================================
#pragma once

#include <string>
#include <vector>

#include "constraints.hpp"

namespace gmdr::config {

enum class DependencyState {
    Ready,          // є і працює
    Missing,        // не встановлено / не знайдено
    NotConfigured,  // потрібне налаштування (ключ API, папка)
    Failed,         // є, але не працює
    Unknown,        // не перевірялося
};
const char* dependency_state_id(DependencyState s);   // ready / missing / notConfigured / failed / unknown

enum class DependencyKind {
    Bundled,   // іде з програмою
    Game,      // копія гри (для рендерера)
    External,  // окремо встановлюється (whisper, OmniVoice)
    Service,   // онлайн-сервіс з ключем
};

struct DependencyStatus {
    std::string     id;            // game.standard, whisper.cli, whisper.model, tts.omnivoice, translate.deepl ...
    std::string     label;         // перекладено
    std::string     purpose;       // для чого (перекладено)
    DependencyKind  kind = DependencyKind::Bundled;
    DependencyState state = DependencyState::Unknown;
    std::string     detail;        // шлях, версія або чому ні
    bool            required = false;   // потрібне поточним налаштуванням
    ActionId        action = ActionId::None;
    std::string     url;           // де взяти (якщо є)
};

std::vector<DependencyStatus> dependencies(const render::RenderSettings& s, const EnvironmentCapabilities& env);

// Для журналу і CLI: «✓ whisper-cli — C:\...\whisper-cli.exe»
std::string format_dependency(const DependencyStatus& d);

} // namespace gmdr::config
