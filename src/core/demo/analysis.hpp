// =============================================================================
//  analysis.hpp — повний аналіз демо: інформація, гравці, голосові пакети.
// =============================================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "chat.hpp"
#include "demo_file.hpp"
#include "netmessages.hpp"
#include "string_tables.hpp"

namespace gmdr::demo {

struct VoicePacketRef {
    int32_t              tick = 0;      // тік демо, коли пакет надійшов
    int                  client = -1;   // слот мовця
    bool                 proximity = false;
    bool                 scavenged = false;   // знайдено пошуком у пошкодженому пакеті
    std::vector<uint8_t> data;          // пакет Steam Voice
};

struct DemoAnalysis {
    std::filesystem::path       path;
    DemoHeader                  header;
    bool                        has_server_info = false;
    ServerInfoMsg               server_info;
    float                       tick_interval = 1.0f / 66.0f;
    int32_t                     first_tick = 0;     // перший тік пакетів гри (після signon)
    int32_t                     last_tick = 0;
    double                      duration_seconds = 0.0;
    int                         local_slot = -1;    // слот того, хто записував
    std::map<int, PlayerInfo>   players;            // слот -> гравець (останній стан)
    std::string                 voice_codec;
    int                         voice_quality = 0;
    std::vector<VoicePacketRef> voice_packets;
    ProtocolVariant             variant;
    int                         packets_total = 0;
    int                         packets_failed = 0;
    int                         voice_scavenged = 0;
    std::map<int, int>          fail_types;         // тип повідомлення -> кількість збоїв
    std::vector<std::string>    console_commands;   // dem_consolecmd (перші кілька)
    std::vector<DemoEvent>      events;             // чат, входи/виходи гравців (за часом)
    UserMessageTypes            user_message_types; // які user messages виявились SayText/TextMsg
    std::vector<std::string>    warnings;

    double tick_to_seconds(double tick) const { return tick * tick_interval; }
    size_t count_events(DemoEventKind k) const;
    std::string player_name(int slot) const;
};

struct AnalyzeOptions {
    bool collect_voice = true;
    bool collect_events = true;
    int  detect_packets = 3000;   // скільки пакетів використовувати для визначення варіанту протоколу
};

using ProgressFn = std::function<void(double fraction)>;

// Кидає std::runtime_error, якщо файл не є демо GMod.
DemoAnalysis analyze_demo(const std::filesystem::path& path, const AnalyzeOptions& opt = {},
                          const ProgressFn& progress = {}, const std::atomic<bool>* cancel = nullptr);
DemoAnalysis analyze_demo(DemoFile& file, const AnalyzeOptions& opt = {}, const ProgressFn& progress = {},
                          const std::atomic<bool>* cancel = nullptr);

// Визначити варіант протоколу за першими пакетами.
ProtocolVariant detect_protocol_variant(DemoFile& file, int max_packets, int* failures_out = nullptr);

// Пошук svc_VoiceData у пакеті, який не вдалося розібрати (перевірка за CRC Steam Voice).
std::vector<VoiceDataMsg> scavenge_voice(const uint8_t* data, size_t size, size_t start_bit);

} // namespace gmdr::demo
