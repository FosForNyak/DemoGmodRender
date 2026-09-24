#include "game_renderer.hpp"

#include "rtx.hpp"

#include "../util/i18n.hpp"
#include "../util/log.hpp"
#include "../util/strings.hpp"

namespace gmdr::game {

namespace fs = std::filesystem;

namespace {

// ---- Звичайний Garry's Mod зі Steam ----
class StandardRenderer final : public GameRenderer {
public:
    std::string id() const override { return "standard"; }
    std::string label() const override { return tr("Стандарт"); }
    std::string description() const override {
        return tr("Звичайна гра зі Steam. Вікно гри може бути за межами екрана, кілька копій гри рендерять фрагмент "
                  "частинами одночасно.");
    }
    const RendererTraits& traits() const override {
        static const RendererTraits t = [] {
            RendererTraits r;
            r.max_parallel = 4;
            r.dir_setting = "game_dir";
            return r;
        }();
        return t;
    }
    std::string not_found_message() const override {
        return tr("Garry's Mod не знайдено — вкажіть папку гри вручну (…\\steamapps\\common\\GarrysMod).");
    }
    std::optional<GModInstall> detect(std::vector<std::string>* log) const override { return detect_gmod(log); }
};

// ---- GMod RTX (RTX Remix) від RTXLauncher ----
// Перевірено: за межами екрана Remix віддає чорні кадри; денойзеру потрібна історія кадрів
// (довший розгін); перший кадр чекає на компіляцію шейдерів (без кешу — хвилини); кілька копій
// і дописування після збою з RTX не перевірені.
class RtxRenderer final : public GameRenderer {
public:
    std::string id() const override { return "rtx"; }
    std::string label() const override { return "GMod RTX"; }
    std::string description() const override {
        return tr("Копія GMod RTX від RTXLauncher: відео з трасуванням променів (RTX Remix). Запускається з тими самими "
                  "параметрами, що й у RTXLauncher, і довше розганяє демо, щоб денойзер встиг зібрати історію кадрів. "
                  "Рендер значно повільніший — спершу зробіть тестовий прогін.");
    }
    const RendererTraits& traits() const override {
        static const RendererTraits t = [] {
            RendererTraits r;
            r.max_parallel = 1;
            r.offscreen_window = false;
            r.needs_64bit = true;
            r.resume = false;
            r.warmup_seconds = 10.0;
            r.load_timeout_seconds = 1800;
            r.hang_seconds = 90;
            r.busy_wait_minutes = 20;
            r.black_frame_check = true;
            r.busy_hint = tr("Remix компілює шейдери?");
            // Так гру запускає сам RTXLauncher; файли гри в копії пропатчені, тож VAC вимкнено (-insecure)
            r.launch_args = {"-dxlevel", "90", "-nod3d9ex", "+mat_disable_d3d9ex", "1", "-insecure"};
            r.dir_setting = "rtx_game_dir";
            return r;
        }();
        return t;
    }
    std::string not_found_message() const override {
        return tr("Копію GMod RTX не знайдено — встановіть її через RTXLauncher або вкажіть папку.");
    }
    std::string install_url() const override { return "https://github.com/Xenthio/RTXLauncher"; }
    std::optional<GModInstall> detect(std::vector<std::string>* log) const override { return detect_rtx_install(log); }
    bool accepts(const GModInstall& g) const override { return is_rtx_install(g); }

    void prepare(const GModInstall& g, const fs::path& backup_dir) const override {
        if (!is_rtx_install(g))
            log_warn("{}", trf("Увімкнено RTX, але в папці гри немає rtx.conf чи rtx-remix — це точно копія від RTXLauncher?"));
        // Налаштування Remix для офлайн-рендеру; оригінал повернеться після рендеру (і після збою)
        const fs::path backup = backup_dir / "rtx.conf.bak";
        std::error_code ec;
        std::string err;
        if (fs::exists(backup, ec)) {
            log_info("{}", trf("rtx.conf уже налаштовано для рендеру попереднім пунктом черги"));
        } else if (apply_rtx_render_profile(g, backup, &err)) {
            log_info("{}", trf("rtx.conf: на час рендеру — повна роздільна здатність (DLAA), без генерації кадрів і заставки"));
            log_info("{}", trf("RTX: шейдери Remix компілюються до кадру, а не у фоні — кадр чекає на них, а не виходить "
                               "чорним (перша компіляція без кешу може тривати кілька хвилин)"));
        } else {
            log_warn("{}", trf("Не вдалося змінити rtx.conf ({}) — Remix рендеритиме з вашими налаштуваннями", err));
        }
    }
    bool restore(const GModInstall& g, const fs::path& backup_dir, bool remove) const override {
        const fs::path backup = backup_dir / "rtx.conf.bak";
        std::error_code ec;
        if (!fs::exists(backup, ec)) return false;
        restore_rtx_profile(g, backup);
        if (remove) fs::remove(backup, ec);
        return true;
    }
    void after_render(const GModInstall& g) const override {
        // Звірка з журналом Remix: чи прийняв він налаштування для рендеру
        const auto eff = read_remix_effective_options(g);
        for (const auto& [k, v] : rtx_render_profile_values()) {
            auto it = eff.find(k);
            if (it == eff.end()) continue;   // Remix пише лише значення, відмінні від типових
            if (it->second != v)
                log_warn("{}", trf("Remix: {} = {} замість {} — налаштування користувача в меню Remix мають вищий пріоритет", k,
                                   it->second, v));
            else
                log_debug("Remix прийняв {} = {}", k, v);
        }
    }
};

} // namespace

const std::vector<const GameRenderer*>& game_renderers() {
    static const StandardRenderer standard;
    static const RtxRenderer rtx;
    static const std::vector<const GameRenderer*> all = {&standard, &rtx};
    return all;
}

const GameRenderer* find_game_renderer(const std::string& id) {
    const std::string key = to_lower(trim(id));
    for (const auto* r : game_renderers())
        if (r->id() == key) return r;
    return nullptr;
}

const GameRenderer& standard_renderer() { return *game_renderers().front(); }

} // namespace gmdr::game
