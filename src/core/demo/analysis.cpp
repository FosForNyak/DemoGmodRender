#include "analysis.hpp"

#include "../util/log.hpp"
#include "../voice/steam_voice.hpp"
#include "bitreader.hpp"

#include <algorithm>
#include <climits>
#include <cstring>
#include <format>
#include <stdexcept>

namespace gmdr::demo {

std::string DemoAnalysis::player_name(int slot) const {
    auto it = players.find(slot);
    if (it != players.end() && !it->second.name.empty()) return it->second.name;
    if (slot == local_slot && !header.client_name.empty()) return header.client_name;
    return std::format("Гравець #{}", slot);
}

namespace {

class Collector final : public NetHandler {
public:
    Collector(DemoAnalysis& a, StringTableSet& st, bool voice) : a_(a), st_(st), voice_(voice) {}
    bool wants_string_tables() const override { return true; }
    bool wants_voice() const override { return voice_; }

    void on_server_info(const ServerInfoMsg& si) override {
        a_.server_info = si;
        a_.has_server_info = true;
        a_.tick_interval = si.tick_interval;
        a_.local_slot = si.player_slot;
    }
    void on_create_string_table(const CreateStringTableMsg& m) override {
        if (!st_.on_create(m)) warn(std::format("таблиця рядків '{}': {}", m.name, st_.last_error()));
    }
    void on_update_string_table(const UpdateStringTableMsg& m) override {
        if (!st_.on_update(m)) warn(std::format("оновлення таблиці: {}", st_.last_error()));
        else refresh_players();
    }
    void on_voice_init(const VoiceInitMsg& m) override {
        a_.voice_codec = m.codec;
        a_.voice_quality = m.quality;
    }
    void on_voice_data(const VoiceDataMsg& m) override {
        VoicePacketRef v;
        v.tick = tick_;
        v.client = m.client;
        v.proximity = m.proximity;
        v.data = m.data;
        // Довжина в бітах має бути кратна 8 — обрізаємо "хвіст"
        v.data.resize(m.data_bits / 8);
        a_.voice_packets.push_back(std::move(v));
    }
    void refresh_players() {
        for (auto& [slot, pi] : st_.players()) a_.players[slot] = pi;
    }
    void set_tick(int32_t t) { tick_ = t; }
    // Однакові попередження (напр. оновлення тієї самої таблиці) показуємо один раз з лічильником.
    void finish_warnings() {
        for (auto& w : a_.warnings)
            if (auto it = warn_counts_.find(w); it != warn_counts_.end() && it->second > 1)
                w += std::format(" (×{})", it->second);
    }

private:
    void warn(const std::string& w) {
        if (++warn_counts_[w] == 1 && a_.warnings.size() < 50) a_.warnings.push_back(w);
    }
    DemoAnalysis&              a_;
    StringTableSet&            st_;
    bool                       voice_;
    int32_t                    tick_ = 0;
    std::map<std::string, int> warn_counts_;
};

} // namespace

std::vector<VoiceDataMsg> scavenge_voice(const uint8_t* data, size_t size, size_t start_bit) {
    std::vector<VoiceDataMsg> out;
    const size_t total_bits = size * 8;
    // Заголовок svc_VoiceData: 6 (тип) + 8 (клієнт) + 8 (proximity) + 16 (довжина) = 38 біт
    size_t b = start_bit;
    while (b + 38 + 13 * 8 <= total_bits) {
        BitReader br(data, size);
        br.skip_bits(b);
        if (br.read_ubits(6) != static_cast<uint32_t>(svc_VoiceData)) { ++b; continue; }
        const int client = br.read_byte();
        const int prox = br.read_byte();
        const uint32_t bits = br.read_word();
        if (client > 255 || prox > 1 || bits % 8 != 0 || bits < 13 * 8 || bits > br.bits_left()) { ++b; continue; }
        std::vector<uint8_t> payload = br.read_bits_to_bytes(bits);
        if (!voice::looks_like_steam_voice(payload.data(), payload.size())) { ++b; continue; }
        VoiceDataMsg m;
        m.client = client;
        m.proximity = prox != 0;
        m.data = std::move(payload);
        m.data_bits = bits;
        out.push_back(std::move(m));
        b = br.position();
    }
    return out;
}

ProtocolVariant detect_protocol_variant(DemoFile& file, int max_packets, int* failures_out) {
    const auto candidates = candidate_variants();
    std::vector<int> fails(candidates.size(), 0);
    std::vector<int> ok_count(candidates.size(), 0);
    file.rewind();
    DemoCommand cmd;
    int packets = 0;
    while (packets < max_packets && file.next(cmd)) {
        if (cmd.cmd != DemoCmd::Packet && cmd.cmd != DemoCmd::Signon) continue;
        ++packets;
        for (size_t i = 0; i < candidates.size(); ++i) {
            const auto r = parse_packet(cmd.data, cmd.size, candidates[i], nullptr);
            if (r.ok) ++ok_count[i];
            else ++fails[i];
        }
    }
    file.rewind();
    size_t best = 0;
    for (size_t i = 1; i < candidates.size(); ++i)
        if (fails[i] < fails[best]) best = i;   // при рівності — перший (типовий) варіант
    if (failures_out) *failures_out = fails[best];
    for (size_t i = 0; i < candidates.size(); ++i)
        log_debug("Варіант протоколу [{}]: успішно {} / збоїв {}", candidates[i].describe(), ok_count[i], fails[i]);
    return candidates[best];
}

DemoAnalysis analyze_demo(const std::filesystem::path& path, const AnalyzeOptions& opt, const ProgressFn& progress,
                          const std::atomic<bool>* cancel) {
    DemoFile file(path);
    DemoAnalysis a = analyze_demo(file, opt, progress, cancel);
    a.path = path;
    return a;
}

DemoAnalysis analyze_demo(DemoFile& file, const AnalyzeOptions& opt, const ProgressFn& progress,
                          const std::atomic<bool>* cancel) {
    DemoAnalysis a;
    a.header = file.header();
    if (a.header.network_protocol != 24)
        a.warnings.push_back(std::format("Мережевий протокол {} (для GMod очікується 24) — розбір може бути неточним",
                                         a.header.network_protocol));
    if (!a.header.game_dir.empty() && a.header.game_dir != "garrysmod")
        a.warnings.push_back(std::format("Демо записане у грі '{}', а не в Garry's Mod", a.header.game_dir));

    int detect_fail = 0;
    a.variant = detect_protocol_variant(file, opt.detect_packets, &detect_fail);
    log_info("Варіант протоколу: {} (збоїв на пробі: {})", a.variant.describe(), detect_fail);

    StringTableSet tables;
    Collector collector(a, tables, opt.collect_voice);

    file.rewind();
    DemoCommand cmd;
    bool first_packet_seen = false;
    int32_t min_tick = INT32_MAX, max_tick = 0;
    size_t last_progress_pos = 0;
    while (file.next(cmd)) {
        if (cancel && cancel->load()) throw std::runtime_error("Аналіз скасовано");
        if (progress && file.position() - last_progress_pos > (1u << 20)) {
            last_progress_pos = file.position();
            progress(static_cast<double>(file.position()) / static_cast<double>(file.file_size()));
        }
        switch (cmd.cmd) {
        case DemoCmd::Signon:
        case DemoCmd::Packet: {
            collector.set_tick(cmd.tick);
            ++a.packets_total;
            const auto r = parse_packet(cmd.data, cmd.size, a.variant, &collector);
            if (!r.ok) {
                ++a.packets_failed;
                ++a.fail_types[r.fail_type];
                if (a.packets_failed <= 5)
                    log_debug("Пакет на тіку {}: {} (тип {}, біт {} з {})", cmd.tick, r.error, r.fail_type, r.fail_bit,
                              cmd.size * 8);
                if (opt.collect_voice) {
                    // Шукаємо голос у решті пакета — Steam Voice має CRC, тож хибних збігів не буде.
                    const size_t from = r.fail_bit >= 6 ? r.fail_bit - 6 : 0;
                    for (auto& m : scavenge_voice(cmd.data, cmd.size, from)) {
                        collector.on_voice_data(m);
                        a.voice_packets.back().scavenged = true;
                        ++a.voice_scavenged;
                    }
                }
            }
            if (cmd.cmd == DemoCmd::Packet) {
                if (!first_packet_seen) {
                    first_packet_seen = true;
                }
                min_tick = std::min(min_tick, cmd.tick);
                max_tick = std::max(max_tick, cmd.tick);
            }
            break;
        }
        case DemoCmd::StringTables:
            tables.on_snapshot(cmd.data, cmd.size);
            if (tables.snapshot_truncated())
                log_debug("Знімок таблиць рядків обрізаний грою ({} байт) — використано таблиці до місця обриву", cmd.size);
            collector.refresh_players();
            break;
        case DemoCmd::ConsoleCmd:
            if (a.console_commands.size() < 200 && cmd.size > 0)
                a.console_commands.emplace_back(reinterpret_cast<const char*>(cmd.data),
                                                strnlen(reinterpret_cast<const char*>(cmd.data), cmd.size));
            break;
        default:
            break;
        }
    }
    if (!file.error().empty()) a.warnings.push_back("Файл демо: " + file.error());
    if (!file.reached_stop()) a.warnings.push_back("Демо не має маркера кінця (можливо, запис перервано) — використано наявні дані");
    collector.refresh_players();
    collector.finish_warnings();

    a.first_tick = first_packet_seen ? min_tick : 0;
    a.last_tick = first_packet_seen ? max_tick : 0;
    if (a.header.playback_ticks > 0) a.last_tick = std::max(a.last_tick, a.header.playback_ticks);
    if (!(a.tick_interval > 0)) a.tick_interval = 1.0f / 66.0f;
    a.duration_seconds = a.last_tick * static_cast<double>(a.tick_interval);

    if (a.packets_failed > 0) {
        std::string types;
        for (auto& [t, n] : a.fail_types) types += std::format(" {}×{}", net_message_name(t), n);
        a.warnings.push_back(std::format("Не вдалося повністю розібрати {} з {} пакетів (на:{}). Голос шукали за CRC, знайдено додатково {}.",
                                         a.packets_failed, a.packets_total, types, a.voice_scavenged));
    }
    if (progress) progress(1.0);
    std::stable_sort(a.voice_packets.begin(), a.voice_packets.end(),
                     [](const VoicePacketRef& x, const VoicePacketRef& y) { return x.tick < y.tick; });
    return a;
}

} // namespace gmdr::demo
