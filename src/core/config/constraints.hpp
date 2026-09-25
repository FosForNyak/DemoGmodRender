// =============================================================================
//  constraints.hpp — перевірка налаштувань: що можна поєднати в цьому середовищі.
//
//  Три окремі речі:
//    * налаштування (render::RenderSettings) — чого хоче користувач;
//    * середовище (EnvironmentCapabilities) — що вміє цей комп'ютер;
//    * результат перевірки (ValidationResult) — чи це можна виконати, і що саме
//      вийде (похідні величини: скільки копій гри, який формат пікселів, скільки
//      кадрів має відрендерити гра...).
//
//  evaluate() проганяє правила (rules.cpp) — невеликі функції з ідентифікаторами
//  ("parallel.renderer_limit", "video.codec_container" ...). Кожне правило додає
//  зауваження (Issue) трьох рівнів:
//    Error   — рендер неможливий (кнопка «Почати рендер» вимкнена, CLI і черга
//              відмовляють ще до запуску гри);
//    Warning — можна, але щось буде пропущено, повільно чи ризиковано;
//    Info    — корисний наслідок.
//  і описує стан налаштувань (SettingState): видиме / доступне, дозволені
//  значення (з причиною для недоступних), межі.
//
//  Нічого не змінюється мовчки: виправлення (Fix) — це запропоновані зміни, які
//  застосовує користувач (apply_fix). Ті самі правила використовують вікно, CLI,
//  черга (перед кожним пунктом) і сам рендер (перед запуском гри).
// =============================================================================
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "capabilities.hpp"
#include "settings_catalog.hpp"

namespace gmdr::config {

enum class Severity { Info, Warning, Error };
const char* severity_id(Severity s);   // info / warning / error

enum class IssueKind {
    Availability,   // функції чи компонента немає в цьому середовищі
    Dependency,     // функція потребує іншої
    Conflict,       // два налаштування разом неможливі
    Range,          // значення поза межами (межі можуть залежати від інших налаштувань)
    Resource,       // можливо, але важко для цього ПК
    Runtime,        // перевіриться лише під час запуску
    Platform,       // лише на певній ОС
    External,       // потрібен окремо встановлений компонент
    Renderer,       // обмеження рендерера гри
    Consequence,    // наслідок вибору (інформація)
};
const char* kind_id(IssueKind k);

struct SettingChange {
    SettingId   id;
    json::Value value;
};

// Запропоноване виправлення: детерміновані зміни налаштувань
struct Fix {
    std::string                label;   // «Одна копія гри», «Кодек H.264 (x264)»
    std::vector<SettingChange> changes;
};

// Дія, яка не змінює налаштувань (встановити компонент, знайти гру...)
enum class ActionId {
    None,
    OpenDemo,
    DetectGame,
    InstallWhisperModel,
    InstallWhisperCli,
    InstallVoiceEngine,
    ConfigureTranslator,
    ConfigureElevenLabs,
    ConfirmVoiceConsent,
    CloseGame,
};
const char* action_id(ActionId a);

struct Issue {
    std::string            rule;         // ідентифікатор правила
    Severity               severity = Severity::Info;
    IssueKind              kind = IssueKind::Consequence;
    SettingId              setting = SettingId::Count;   // Count — загальне
    std::vector<SettingId> related;
    std::string            message;      // що не так (перекладено)
    std::string            explanation;  // чому і від чого залежить (перекладено)
    std::vector<Fix>       fixes;
    ActionId               action = ActionId::None;
};

struct OptionState {
    std::string value;
    std::string label;             // перекладено
    bool        available = true;
    std::string reason;            // чому недоступне (або застереження, якщо warning)
    std::string group;             // «Процесор (CPU)», «Відеокарта (GPU)» ...
    bool        recommended = false;
    bool        warning = false;   // доступне, але з застереженням
};

struct SettingState {
    bool                     visible = true;
    bool                     enabled = true;
    std::string              reason;     // чому вимкнене чи сховане
    std::vector<OptionState> options;    // для переліків (порожньо — вільне значення)
    std::optional<double>    min, max;
    std::string              note;       // коротко біля поля: «Обмежено GMod RTX»
};

// Що вийде з цих налаштувань (одні розрахунки для вікна, CLI і рендеру)
struct Derived {
    std::string renderer;             // id рендерера
    std::string container;
    bool        image_sequence = false;
    double      fps = 0;              // кадрів/с відео (0 — неправильне значення)
    int         subframes = 1;        // під-кадрів гри на кадр відео (motion blur)
    double      speed = 1.0;
    double      game_fps = 0;         // кадрів/с часу демо, які рендерить гра
    double      demo_seconds = 0;     // тривалість фрагмента в часі демо (0 — невідомо)
    double      video_seconds = 0;    // тривалість відео
    int64_t     video_frames = 0;
    int64_t     game_frames = 0;      // скільки кадрів має відрендерити гра
    int         width = 0, height = 0;             // кадр відео (після округлення до парних)
    int         game_width = 0, game_height = 0;   // вікно гри
    std::string pix_fmt;              // формат пікселів кодера
    int         bit_depth = 8;        // фактична бітність
    int         chroma = 420;         // фактична субдискретизація
    int         parallel_max = 1;     // скільки копій гри дозволяє рендерер
    int         parallel = 1;         // скільки копій гри справді рендеритиме
    std::string parallel_reason;      // чому менше, ніж вибрано
    std::string window_mode;          // фактичне вікно гри
    int64_t     video_bitrate = 0;    // біт/с: явний чи під розмір файлу (0 — за якістю)
    int64_t     audio_bitrate = 0;
    uint64_t    estimated_bytes = 0;  // орієнтовний розмір (0 — невідомо: якість без бітрейту)
    bool        audio_in_file = false;
    std::vector<std::string> extra_outputs;   // id додаткових версій, що справді зробляться
};

struct ValidationContext {
    enum class Purpose { Edit, Render, TestRun, QueueItem };
    Purpose purpose = Purpose::Edit;
    // Демо після аналізу: тривалість фрагмента і голоси. Без нього ці правила мовчать.
    bool    demo_known = false;
    double  tick_interval = 0;
    int32_t last_tick = 0;
    int     speakers = -1;           // гравців із голосом (-1 — невідомо)
    int     local_speaker = -1;      // є голос того, хто записав (-1 — невідомо)
};

class ValidationResult {
public:
    std::vector<Issue>        issues;
    std::vector<SettingState> states;   // за SettingId
    Derived                   derived;

    bool executable() const { return count(Severity::Error) == 0; }
    bool clean() const { return issues.empty(); }
    int  count(Severity s) const;
    const SettingState&       state(SettingId id) const { return states[static_cast<size_t>(id)]; }
    std::vector<const Issue*> issues_for(SettingId id) const;   // головне чи пов'язане налаштування
    const Issue*              find(const std::string& rule) const;
};

ValidationResult evaluate(const render::RenderSettings& s, const EnvironmentCapabilities& env,
                          const ValidationContext& ctx = {});

// Застосувати виправлення (новий набір налаштувань; вихідний не змінюється)
render::RenderSettings apply_fix(const render::RenderSettings& s, const Fix& fix);

// Правила — для діагностики і документації
struct RuleInfo {
    std::string id;
    std::string description;
};
std::vector<RuleInfo> rule_list();

// Рядок для журналу і консолі: «Помилка: … Чому: … Виправлення: …» (без рівня — для журналу,
// де рівень уже видно)
std::string format_issue(const Issue& i, bool with_severity = true);
// Коротко: «1 помилка, 2 попередження»
std::string summary_text(const ValidationResult& r);

} // namespace gmdr::config
