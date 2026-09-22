// =============================================================================
//  jobs.hpp — фонові завдання: аналіз демо, рендер через гру, кодування
//  готових кадрів. Кожне завдання працює у своєму потоці, а GUI/CLI лише
//  читають progress() і можуть скасувати.
// =============================================================================
#pragma once

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../demo/analysis.hpp"
#include "../game/gmod_install.hpp"
#include "../game/lua_driver.hpp"
#include "../voice/voice_decoder.hpp"
#include "encode_session.hpp"
#include "settings.hpp"

namespace gmdr::render {

enum class JobState { Idle, Running, Succeeded, Failed, Cancelled };

// Крок перевірки (тестовий прогін і звичайний рендер): що вже спрацювало.
struct CheckItem {
    enum State { Pending, Ok, Failed, Skipped };
    std::string name;
    State       state = Pending;
    std::string detail;
};

struct Progress {
    std::string stage = "Очікування";
    double      fraction = 0.0;        // 0..1 загальний прогрес (-1 — невідомо)
    int32_t     demo_tick = 0;
    int32_t     demo_total = 0;
    int32_t     range_start = 0, range_end = 0;
    int64_t     subframes = 0;
    int64_t     frames = 0;
    double      video_seconds = 0;
    double      expected_seconds = 0;
    int64_t     pending_files = 0;
    uint64_t    pending_bytes = 0;
    double      speed_fps = 0;         // кадрів відео за секунду реального часу
    double      elapsed = 0;
    double      eta = -1;
    int64_t     bytes_written = 0;
    bool        game_running = false;
    bool        game_paused = false;
    bool        disk_low = false;      // гру призупинено: закінчується місце на диску
    bool        game_hidden = false;   // вікно гри за межами екрана / позаду інших
    std::string driver_state;
    std::string video_desc, audio_desc;
    // Середній час етапів конвеєра, мс (читання і декодування — на під-кадр гри,
    // змішування — на під-кадр, колір/кодування/звук — на кадр відео)
    double      stat_read_ms = 0, stat_decode_ms = 0, stat_blend_ms = 0;
    double      stat_convert_ms = 0, stat_encode_ms = 0, stat_audio_ms = 0;
    double      stat_game_wait = -1;   // частка часу запису, коли програма чекала на кадри гри (0..1)
    std::vector<CheckItem> checks;
};

class Job {
public:
    virtual ~Job();
    void start();
    void cancel();        // м'яко: зупинити запис і зберегти вже зроблене
    void kill();          // жорстко: негайно закрити гру
    void wait();
    JobState    state() const { return state_.load(); }
    bool        running() const { return state_ == JobState::Running; }
    Progress    progress() const;
    std::string error() const;
    std::string result() const;
    // Підсумковий звіт (тестовий прогін: кроки, швидкість, прогноз часу й розміру).
    std::string report() const;
    virtual std::string name() const = 0;
    // Живе прев'ю кадрів, що кодуються.
    const PreviewSink& preview() const { return *preview_; }
    // Кнопка "Показати гру": тимчасово повернути вікно гри на екран.
    virtual bool can_show_game() const { return false; }
    virtual void set_show_game(bool /*show*/) {}

protected:
    virtual void run() = 0;
    void set_stage(const std::string& stage, double fraction = -1);
    template <class F>
    void update(F&& f) {
        std::lock_guard lock(mutex_);
        f(progress_);
    }
    void fail(const std::string& message);
    void succeed(const std::string& result);
    void set_report(const std::string& report);
    double elapsed_seconds() const;

    std::atomic<bool>     cancel_{false};
    std::atomic<bool>     kill_{false};
    std::shared_ptr<PreviewSink> preview_ = std::make_shared<PreviewSink>();

private:
    std::atomic<JobState> state_{JobState::Idle};
    mutable std::mutex    mutex_;
    Progress              progress_;
    std::string           error_, result_, report_;
    std::thread           thread_;
    std::chrono::steady_clock::time_point started_;
    std::atomic<int64_t>  ended_ns_{0};   // скільки тривало завдання (нс); 0 — ще триває
    void mark_ended();
};

// ---- Аналіз демо + декодування голосу ------------------------------------------
class AnalyzeJob final : public Job {
public:
    explicit AnalyzeJob(std::string demo_path) : path_(std::move(demo_path)) {}
    std::string name() const override { return "Аналіз демо"; }
    std::shared_ptr<const demo::DemoAnalysis>      analysis() const;
    std::shared_ptr<const voice::VoiceDecodeResult> voices() const;

protected:
    void run() override;

private:
    std::string path_;
    mutable std::mutex m_;
    std::shared_ptr<const demo::DemoAnalysis>       analysis_;
    std::shared_ptr<const voice::VoiceDecodeResult> voices_;
};

// Вибрати доріжки голосу згідно з налаштуваннями.
std::vector<const voice::SpeakerTrack*> select_speakers(const RenderSettings& s, const voice::VoiceDecodeResult& v);

// ---- Рендер через гру ------------------------------------------------------------
class RenderJob final : public Job {
public:
    // test_run: тестовий прогін — 3 секунди з початку фрагмента в тимчасовий файл зі
    // звітом по кроках, заміром швидкості і прогнозом часу й розміру всього рендеру.
    RenderJob(RenderSettings s, std::shared_ptr<const demo::DemoAnalysis> analysis,
              std::shared_ptr<const voice::VoiceDecodeResult> voices, bool test_run = false);
    std::string name() const override { return test_run_ ? "Тестовий прогін" : "Рендер демо"; }
    bool is_test_run() const { return test_run_; }
    bool can_show_game() const override;
    void set_show_game(bool show) override { show_game_ = show; }

    static constexpr double kTestSeconds = 3.0;

protected:
    void run() override;

private:
    bool prepare(std::string* error);
    // game_closing: гру щойно закрито — дочекатися, поки зникнуть і її допоміжні процеси
    // (браузер CEF у GMod — теж gmod.exe), інакше config.cfg не відновився б.
    void cleanup(bool game_closing = true);
    void set_check(int index, CheckItem::State state, const std::string& detail = {});
    // Унікальний префікс файлів кадрів: gmdr_<id>_0000.tga ...
    std::string movie_prefix() const { return "gmdr_" + id_ + "_"; }

    RenderSettings                                  s_;
    std::shared_ptr<const demo::DemoAnalysis>       analysis_;
    std::shared_ptr<const voice::VoiceDecodeResult> voices_;
    std::optional<game::GModInstall>                gmod_;
    std::string                                     id_;
    std::filesystem::path                           tmp_dir_;
    std::filesystem::path                           config_backup_;
    std::filesystem::path                           stray_dir_;   // куди гра насправді писала кадри (якщо не в tmp)
    bool                                            job_written_ = false;
    bool                                            test_run_ = false;
    std::atomic<bool>                               show_game_{false};
};

// ---- Перегляд демо в грі з вибраного місця ----------------------------------------
// Гра запускається звичайно (у фокусі, зі звуком), демо перемотується до from_tick і
// грає в реальному часі. Клавіші в грі: F9 — початок фрагмента, F11 — кінець, F6 — позначка;
// програма забирає їх через take_marks(). Завдання закінчується, коли гравець закриє гру.
class WatchJob final : public Job {
public:
    WatchJob(RenderSettings s, std::shared_ptr<const demo::DemoAnalysis> analysis, int32_t from_tick);
    std::string name() const override { return "Перегляд у грі"; }
    std::vector<game::DriverMark> take_marks();

protected:
    void run() override;

private:
    RenderSettings                            s_;
    std::shared_ptr<const demo::DemoAnalysis> analysis_;
    int32_t                                   from_tick_ = 0;
    std::mutex                                marks_mutex_;
    std::vector<game::DriverMark>             marks_;
};

// ---- Кодування вже готових кадрів (TGA/JPG послідовність від startmovie) ------------
class EncodeFramesJob final : public Job {
public:
    // frames_dir — папка з кадрами; prefix — порожньо для автопошуку.
    EncodeFramesJob(RenderSettings s, std::filesystem::path frames_dir, std::string prefix,
                    std::filesystem::path wav_path, std::shared_ptr<const demo::DemoAnalysis> analysis,
                    std::shared_ptr<const voice::VoiceDecodeResult> voices);
    std::string name() const override { return "Кодування кадрів"; }

protected:
    void run() override;

private:
    RenderSettings                                  s_;
    std::filesystem::path                           dir_;
    std::string                                     prefix_;
    std::filesystem::path                           wav_;
    std::shared_ptr<const demo::DemoAnalysis>       analysis_;
    std::shared_ptr<const voice::VoiceDecodeResult> voices_;
};

// ---- Збереження голосів гравців в окремі аудіофайли ---------------------------------
// Кожен файл охоплює той самий відрізок демо (весь запис або фрагмент з налаштувань),
// тож у програмі монтажу їх достатньо покласти на початок. Формат — WAV, а якщо
// разом вони займали б забагато місця (довгі демо) — FLAC без втрат.
class ExportVoicesJob final : public Job {
public:
    ExportVoicesJob(RenderSettings s, std::shared_ptr<const demo::DemoAnalysis> analysis,
                    std::shared_ptr<const voice::VoiceDecodeResult> voices, std::filesystem::path out_dir);
    std::string name() const override { return "Збереження голосів"; }

protected:
    void run() override;

private:
    RenderSettings                                  s_;
    std::shared_ptr<const demo::DemoAnalysis>       analysis_;
    std::shared_ptr<const voice::VoiceDecodeResult> voices_;
    std::filesystem::path                           dir_;
};

// Автовизначення префікса кадрів у папці (найчисленніша група name####.tga).
std::string detect_frame_prefix(const std::filesystem::path& dir, int64_t* count = nullptr);

// Відновлення після аварійного завершення (повернути config.cfg, прибрати тимчасове).
void recover_leftovers(const game::GModInstall& g);

// Шлях виходу за замовчуванням: поруч із демо, те саме ім'я + розширення.
std::string default_output_path(const std::string& demo_path, const std::string& container_ext);

// Бітрейт відео (біт/с), щоб файл тривалістю seconds важив приблизно target_mb МБ
// (з запасом на контейнер і звук). Для пресету "Discord".
int64_t bitrate_for_target_size(double target_mb, double seconds, int64_t audio_bitrate);

} // namespace gmdr::render
