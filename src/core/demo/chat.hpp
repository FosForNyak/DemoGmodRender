// =============================================================================
//  chat.hpp — чат і події гравців з демо (для шкали часу, пошуку і експорту).
//
//  Звідки що береться (перевірено на справжніх демо GMod 2025–2026):
//   * чат гри — user message SayText: [байт: сутність гравця][рядок][3 байти прапорців].
//     Номер SayText серед user messages у різних збірках GMod різний, тому тип
//     визначається за формою повідомлень (detect_user_message_types);
//   * повідомлення сервера в чат — TextMsg з призначенням HUD_PRINTTALK;
//   * аддони чату (customchat.say тощо) — net-повідомлення з "chat" у назві;
//     текст береться з JSON-поля "text" або з першого рядка в даних;
//   * вхід/вихід/зміна імені — ігрові події player_connect_client,
//     player_disconnect, player_changename; вбивства — player_death, якщо сервер
//     їх надсилає (клієнт GMod зазвичай на них не підписаний).
// =============================================================================
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "netmessages.hpp"

namespace gmdr::demo {

enum class DemoEventKind : uint8_t { Chat, Server, Join, Leave, NameChange, Kill };

struct DemoEvent {
    int32_t       tick = 0;
    DemoEventKind kind = DemoEventKind::Chat;
    int           slot = -1;      // гравець (слот), якщо відомий
    std::string   who;            // ім'я на момент події
    std::string   text;           // текст повідомлення / причина виходу / нове ім'я
    std::string   channel;        // канал аддона чату, "team" тощо
};

const char* event_kind_name(DemoEventKind k);

// Номери SayText і TextMsg серед user messages цього демо (-1, якщо не знайдено).
struct UserMessageTypes {
    int say_text = -1;
    int text_msg = -1;
};

// Збирач зразків user messages для визначення типів: спершу накопичуємо, потім вирішуємо.
class UserMessageClassifier {
public:
    void add(const RawBitsMsg& m);
    UserMessageTypes decide() const;

private:
    struct Score {
        int total = 0, say_like = 0, textmsg_like = 0;
    };
    std::map<int, Score> scores_;
};

// Розбір окремих повідомлень. Повертають false, якщо дані не того формату.
bool parse_say_text(const RawBitsMsg& m, int& entity, std::string& text, bool& team, bool& dead);
bool parse_text_msg(const RawBitsMsg& m, int& dest, std::string& text);
// net-повідомлення аддона чату: name — назва з таблиці networkstring.
bool is_chat_net_message(const std::string& name);
bool parse_chat_net_message(const RawBitsMsg& m, int& entity, std::string& text, std::string& channel);

// Прибирає переводи рядків і зайві пробіли (для показу в один рядок).
std::string clean_text(std::string_view s);

// Подія одним рядком без часу: "Ім'я: текст", "→ Ім'я зайшов на сервер" ...
std::string format_event(const DemoEvent& e);

// Текстовий журнал чату (для збереження у .txt).
std::string format_chat_log(const std::vector<DemoEvent>& events, double tick_interval);

} // namespace gmdr::demo
