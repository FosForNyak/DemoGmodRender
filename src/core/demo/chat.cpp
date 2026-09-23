#include "chat.hpp"

#include "../util/json.hpp"
#include "bitreader.hpp"
#include "../util/strings.hpp"
#include "../util/i18n.hpp"

#include <cmath>
#include <format>

namespace gmdr::demo {

const char* event_kind_name(DemoEventKind k) {
    switch (k) {
    case DemoEventKind::Chat: return "чат";
    case DemoEventKind::Server: return "сервер";
    case DemoEventKind::Join: return "вхід";
    case DemoEventKind::Leave: return "вихід";
    case DemoEventKind::NameChange: return "ім'я";
    case DemoEventKind::Kill: return "вбивство";
    }
    return "?";
}

namespace {

// Довжина коректного UTF-8 рядка з нульовим завершенням від позиції pos (без нуля),
// або -1, якщо до нуля трапляється щось, що не схоже на текст.
long text_length(const std::vector<uint8_t>& d, size_t pos, size_t end) {
    size_t i = pos;
    while (i < end) {
        const uint8_t c = d[i];
        if (c == 0) return static_cast<long>(i - pos);
        if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') return -1;
        int extra = 0;
        if (c >= 0x80) {
            if ((c & 0xE0) == 0xC0) extra = 1;
            else if ((c & 0xF0) == 0xE0) extra = 2;
            else if ((c & 0xF8) == 0xF0) extra = 3;
            else return -1;
            if (i + extra >= end) return -1;
            for (int k = 1; k <= extra; ++k)
                if ((d[i + k] & 0xC0) != 0x80) return -1;
        }
        i += 1 + extra;
    }
    return -1;   // немає нуля в кінці
}

size_t byte_len(const RawBitsMsg& m) { return std::min(m.data.size(), m.data_bits / 8); }

} // namespace

// Переводи рядків і табуляції (напр. у причині бану) — в один пробіл
std::string clean_text(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    bool space = false;
    for (const char c : s) {
        if (c == '\n' || c == '\r' || c == '\t' || c == ' ') {
            space = !out.empty();
            continue;
        }
        if (space) out.push_back(' ');
        space = false;
        out.push_back(c);
    }
    return out;
}

namespace {

} // namespace

bool parse_say_text(const RawBitsMsg& m, int& entity, std::string& text, bool& team, bool& dead) {
    if (m.data_bits % 8 != 0) return false;
    const size_t n = byte_len(m);
    if (n < 5) return false;
    entity = m.data[0];   // 0 — сервер (say з консолі)
    const long len = text_length(m.data, 1, n);
    if (len <= 0) return false;
    const size_t after = 1 + static_cast<size_t>(len) + 1;
    if (n - after != 3) return false;
    for (size_t i = after; i < n; ++i)
        if (m.data[i] > 1) return false;
    text = clean_text(std::string_view(reinterpret_cast<const char*>(m.data.data() + 1), static_cast<size_t>(len)));
    if (text.empty()) return false;
    // Прапорці після тексту: [чат][команда][мертвий] (порядок як в OnPlayerChat)
    team = m.data[after + 1] != 0;
    dead = m.data[after + 2] != 0;
    return true;
}

bool parse_text_msg(const RawBitsMsg& m, int& dest, std::string& text) {
    if (m.data_bits % 8 != 0) return false;
    const size_t n = byte_len(m);
    if (n < 6) return false;
    dest = m.data[0];
    if (dest < 1 || dest > 4) return false;
    size_t pos = 1;
    std::string parts[5];
    for (int s = 0; s < 5; ++s) {
        const long len = text_length(m.data, pos, n);
        if (len < 0) return false;
        parts[s].assign(reinterpret_cast<const char*>(m.data.data() + pos), static_cast<size_t>(len));
        pos += static_cast<size_t>(len) + 1;
    }
    if (pos != n || parts[0].empty()) return false;
    text = parts[0];
    // Параметри %s1..%s4 (зазвичай порожні)
    for (int s = 1; s < 5; ++s) text = replace_all(text, std::format("%s{}", s), parts[s]);
    text = clean_text(text);
    return !text.empty();
}

void UserMessageClassifier::add(const RawBitsMsg& m) {
    auto& s = scores_[m.type];
    ++s.total;
    int e = 0, d = 0;
    std::string t;
    bool a = false, b = false;
    if (parse_say_text(m, e, t, a, b)) ++s.say_like;
    else if (parse_text_msg(m, d, t)) ++s.textmsg_like;
}

UserMessageTypes UserMessageClassifier::decide() const {
    UserMessageTypes out;
    int best_say = 0, best_text = 0;
    for (const auto& [type, s] : scores_) {
        // Тип "свій", якщо так виглядає переважна більшість його повідомлень
        if (s.say_like > best_say && s.say_like * 10 >= s.total * 8) {
            best_say = s.say_like;
            out.say_text = type;
        }
        if (s.textmsg_like > best_text && s.textmsg_like * 10 >= s.total * 8) {
            best_text = s.textmsg_like;
            out.text_msg = type;
        }
    }
    if (out.say_text == out.text_msg) out.text_msg = -1;
    return out;
}

bool is_chat_net_message(const std::string& name) {
    const std::string l = to_lower(name);
    if (l.find("chat") == std::string::npos) return false;
    // лише повідомлення з текстом, а не службові (спавн гравця, налаштування тощо)
    for (const char* w : {"say", "msg", "message", "text", "send", "receive", "broadcast", "add"})
        if (l.find(w) != std::string::npos) return true;
    return false;
}

bool parse_chat_net_message(const RawBitsMsg& m, int& entity, std::string& text, std::string& channel) {
    // [8 біт: тип][16 біт: номер назви][дані]. Дані аддона вирівняні по байту від початку повідомлення.
    if (m.data_bits < 24 + 16) return false;
    const size_t n = byte_len(m);
    entity = -1;
    channel.clear();
    for (size_t pos = 3; pos < n; ++pos) {
        const long len = text_length(m.data, pos, n);
        if (len <= 0) continue;
        std::string s(reinterpret_cast<const char*>(m.data.data() + pos), static_cast<size_t>(len));
        const size_t after = pos + static_cast<size_t>(len) + 1;
        if (s.front() == '{') {
            const auto j = json::parse(s);
            if (!j || !j->is_object()) return false;
            for (const char* k : {"text", "message", "msg"})
                if (text = (*j)[k].as_string(); !text.empty()) break;
            if (text.empty()) return false;
            for (const char* k : {"channel", "mode"})
                if (channel = (*j)[k].as_string(); !channel.empty()) break;
        } else {
            text = s;
        }
        // Відправник: net.WriteEntity пише номер сутності в 13 бітах (MAX_EDICT_BITS) одразу після тексту
        if (m.data_bits >= after * 8 + 13) {
            BitReader br(m.data.data(), m.data.size(), m.data_bits);
            br.skip_bits(after * 8);
            const int ent = static_cast<int>(br.read_ubits(13));
            if (ent >= 1 && ent <= 255) entity = ent;
        }
        text = clean_text(text);
        return !text.empty();
    }
    return false;
}

std::string format_event(const DemoEvent& e) {
    switch (e.kind) {
    case DemoEventKind::Chat:
        return (e.channel.empty() || e.channel == "global" ? "" : "(" + e.channel + ") ") + e.who + ": " + e.text;
    case DemoEventKind::Server: return "* " + e.text;
    case DemoEventKind::Join: return "→ " + e.who + tr(" зайшов на сервер");
    case DemoEventKind::Leave: return "← " + e.who + tr(" вийшов") + (e.text.empty() ? "" : " (" + e.text + ")");
    case DemoEventKind::NameChange: return e.who + tr(" тепер ") + e.text;
    case DemoEventKind::Kill: return "☠ " + e.text;
    }
    return e.text;
}

std::string format_chat_log(const std::vector<DemoEvent>& events, double tick_interval) {
    std::string out;
    for (const auto& e : events) {
        const double t = e.tick * tick_interval;
        const int h = static_cast<int>(t / 3600), mnt = static_cast<int>(std::fmod(t, 3600) / 60),
                  sec = static_cast<int>(std::fmod(t, 60));
        out += (h > 0 ? std::format("[{}:{:02}:{:02}] ", h, mnt, sec) : std::format("[{:02}:{:02}] ", mnt, sec)) +
               format_event(e) + "\n";
    }
    return out;
}

} // namespace gmdr::demo
