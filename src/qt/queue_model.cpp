#include "queue_model.hpp"

#include "config_model.hpp"
#include "project_service.hpp"
#include "qt_convert.hpp"

#include "core/config/formats.hpp"
#include "core/config/presets.hpp"
#include "core/game/game_renderer.hpp"
#include "core/render/markers.hpp"
#include "core/util/file_util.hpp"
#include "core/util/i18n.hpp"
#include "core/util/log.hpp"
#include "core/util/strings.hpp"

#include <QUrl>

#include <algorithm>
#include <filesystem>
#include <format>

namespace gmdr::qt {

namespace fs = std::filesystem;

namespace {
fs::path queue_file() { return app_data_dir() / "gmdr_queue.json"; }
}

QueueModel::QueueModel(ConfigModel* config, ProjectService* project, QObject* parent)
    : QAbstractListModel(parent), config_(config), project_(project) {
    load();
}

void QueueModel::validate(Item& it) const {
    config::ValidationContext ctx;
    ctx.purpose = config::ValidationContext::Purpose::QueueItem;
    config::EnvironmentCapabilities env = config_->environment();
    config::rescan_paths(env, it.s);
    it.check = config::evaluate(it.s, env, ctx);
}

void QueueModel::revalidate() {
    for (auto& it : items_) validate(it);
    if (!items_.empty()) emit dataChanged(index(0), index(count() - 1));
    emit countChanged();
}

int QueueModel::errorItems() const {
    return static_cast<int>(std::count_if(items_.begin(), items_.end(), [](const Item& i) { return !i.check.executable(); }));
}

void QueueModel::load() {
    items_.clear();
    auto text = read_file_text(queue_file());
    if (!text) return;
    auto j = json::parse(*text);
    if (!j || !j->is_array()) return;
    for (const json::Value& e : j->items()) {
        Item it;
        it.s = render::RenderSettings::from_json(e["settings"]);
        it.tick_interval = e["tick_interval"].as_number(0);
        if (it.s.demo_path.empty()) continue;
        validate(it);
        items_.push_back(std::move(it));
    }
    if (!items_.empty()) log_info("{}", trf("Черга рендерів: {} пункт(ів) з минулого разу — сторінка «Черга»", items_.size()));
}

void QueueModel::save() const {
    json::Value arr = json::Value::array();
    for (const auto& it : items_) {
        json::Value e = json::Value::object();
        e.set("tick_interval", json::Value::number(it.tick_interval));
        e.set("settings", it.s.to_json());
        arr.push(e);
    }
    std::string err;
    if (!write_file_atomic(queue_file(), arr.dump(), &err)) log_debug("Не вдалося зберегти чергу: {}", err);
}

int QueueModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : count(); }

QHash<int, QByteArray> QueueModel::roleNames() const {
    return {{DemoRole, "demo"},         {DemoNameRole, "demoName"}, {OutputRole, "output"},     {RendererRole, "renderer"},
            {ResolutionRole, "resolution"}, {FpsRole, "fps"},        {CodecRole, "codec"},       {ParallelRole, "parallel"},
            {FragmentRole, "fragment"}, {StatusRole, "status"},     {ErrorRole, "error"},       {SecondsRole, "seconds"},
            {ErrorsRole, "errors"},     {WarningsRole, "warnings"}, {IssuesRole, "issues"},     {SummaryRole, "summary"}};
}

QVariant QueueModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= count()) return {};
    const Item& it = items_[static_cast<size_t>(index.row())];
    const render::RenderSettings& s = it.s;
    switch (role) {
    case DemoRole: return qs(s.demo_path);
    case DemoNameRole: return qs(path_to_utf8(path_from_utf8(s.demo_path).filename()));
    case OutputRole: return qs(s.output_path);
    case RendererRole: return qs(render::renderer_of(s).label());
    case ResolutionRole: return qs(std::format("{}×{}", s.width, s.height));
    case FpsRole: return qs(s.fps);
    case CodecRole: {
        const auto* e = config::find_video_encoder(s.video_codec);
        return e ? qs(gmdr::tr(e->label.c_str())) : qs(s.video_codec);
    }
    case ParallelRole: return it.check.derived.parallel;
    case FragmentRole: {
        if (s.start_tick <= 0 && s.end_tick <= 0) return qs(gmdr::tr("усе демо"));
        const double ti = it.tick_interval > 0 ? it.tick_interval : 1.0 / 66.0;
        return qs(std::format("{}–{}", format_duration(std::max(0, s.start_tick) * ti),
                              s.end_tick > 0 ? format_duration(s.end_tick * ti) : std::string(gmdr::tr("кінець"))));
    }
    case StatusRole: return qs(it.status);
    case ErrorRole: return qs(it.error);
    case SecondsRole: return it.seconds;
    case ErrorsRole: return it.check.count(config::Severity::Error);
    case WarningsRole: return it.check.count(config::Severity::Warning);
    case IssuesRole: {
        QVariantList out;
        for (const auto& i : it.check.issues) {
            if (i.severity == config::Severity::Info) continue;
            QVariantMap m;
            m["severity"] = config::severity_id(i.severity);
            m["message"] = qs(i.message);
            m["explanation"] = qs(i.explanation);
            out << m;
        }
        return out;
    }
    case SummaryRole: return qs(config::summary_text(it.check));
    default: return {};
    }
}

std::vector<render::RenderSettings> QueueModel::settings_list() const {
    std::vector<render::RenderSettings> out;
    for (const auto& it : items_) out.push_back(it.s);
    return out;
}

void QueueModel::push(Item it) {
    // Той самий файл уже в черзі — не перезаписувати: інша назва
    auto taken = [&](const std::string& p) {
        return std::any_of(items_.begin(), items_.end(), [&](const Item& e) { return to_lower(e.s.output_path) == to_lower(p); });
    };
    if (taken(it.s.output_path) && it.s.output_path.find('%') == std::string::npos) {
        const fs::path p = path_from_utf8(it.s.output_path);
        for (int k = 2; k < 1000; ++k) {
            const std::string cand = path_to_utf8(
                p.parent_path() / path_from_utf8(std::format("{}_{}{}", path_to_utf8(p.stem()), k, path_to_utf8(p.extension()))));
            if (!taken(cand)) {
                it.s.output_path = cand;
                break;
            }
        }
    }
    validate(it);
    log_info("{}", trf("До черги: {} → {}", path_to_utf8(path_from_utf8(it.s.demo_path).filename()), it.s.output_path));
    beginInsertRows({}, count(), count());
    items_.push_back(std::move(it));
    endInsertRows();
    save();
    emit countChanged();
}

bool QueueModel::addCurrent() {
    auto a = project_->analysis();
    if (!a || config_->settings().output_path.empty()) return false;
    Item it;
    it.s = config_->settings();
    it.tick_interval = a->tick_interval;
    push(std::move(it));
    return true;
}

bool QueueModel::addDemo(const QString& qpath) {
    std::string demo = ss(qpath);
    if (demo.rfind("file:", 0) == 0) demo = ss(QUrl(qpath).toLocalFile());
    Item it;
    it.s = config_->settings();
    it.s.demo_path = demo;
    it.s.start_tick = 0;
    it.s.end_tick = -1;
    if (it.s.voice_mode == "selected") it.s.voice_mode = "all";   // вибрані гравці — з іншого демо
    it.s.markers = render::format_markers(render::load_demo_markers(app_data_dir() / "gmdr_markers.json", demo));
    const std::string ext = config::container_of(config_->settings());
    it.s.output_path.clear();
    config::set_container(it.s, ext);
    push(std::move(it));
    return true;
}

void QueueModel::remove(int index) {
    if (running_ || index < 0 || index >= count()) return;
    beginRemoveRows({}, index, index);
    items_.erase(items_.begin() + index);
    endRemoveRows();
    if (editing_ == index) editing_ = -1;
    else if (editing_ > index) --editing_;
    save();
    emit countChanged();
    emit editingChanged();
}

void QueueModel::move(int from, int to) {
    if (running_ || from < 0 || from >= count() || to < 0 || to >= count() || from == to) return;
    beginMoveRows({}, from, from, {}, to > from ? to + 1 : to);
    Item it = std::move(items_[static_cast<size_t>(from)]);
    items_.erase(items_.begin() + from);
    items_.insert(items_.begin() + to, std::move(it));
    endMoveRows();
    editing_ = -1;
    save();
    emit editingChanged();
}

void QueueModel::clear() {
    if (running_) return;
    beginResetModel();
    items_.clear();
    endResetModel();
    editing_ = -1;
    save();
    emit countChanged();
    emit editingChanged();
}

void QueueModel::edit(int index) {
    if (running_ || index < 0 || index >= count()) return;
    const render::RenderSettings item = items_[static_cast<size_t>(index)].s;
    const std::string demo = item.demo_path;
    // Налаштування пункту — у робочий простір; уподобання вікна лишаються свої
    config_->modify([&](render::RenderSettings& s) {
        render::RenderSettings n = item;
        n.ui_language = s.ui_language;
        n.ui_advanced = s.ui_advanced;
        n.ui_theme = s.ui_theme;
        n.ui_accent = s.ui_accent;
        n.ui_scale = s.ui_scale;
        n.ui_graphics_api = s.ui_graphics_api;
        n.demo_path.clear();   // демо відкриється знову (аналіз), вихід — з пункту
        s = n;
    });
    const std::string out = item.output_path;
    project_->open(qs(demo));
    config_->modify([&](render::RenderSettings& s) { s.output_path = out; });
    editing_ = index;
    emit editingChanged();
}

void QueueModel::saveEdit() {
    if (editing_ < 0 || editing_ >= count()) return;
    Item& it = items_[static_cast<size_t>(editing_)];
    it.s = config_->settings();
    if (auto a = project_->analysis()) it.tick_interval = a->tick_interval;
    it.status = "pending";
    it.error.clear();
    validate(it);
    emit dataChanged(index(editing_), index(editing_));
    save();
    editing_ = -1;
    emit editingChanged();
    emit countChanged();
}

void QueueModel::cancelEdit() {
    editing_ = -1;
    emit editingChanged();
}

void QueueModel::set_running(bool on) {
    running_ = on;
    if (on)
        for (auto& it : items_) {
            it.status = "pending";
            it.error.clear();
        }
    if (!items_.empty()) emit dataChanged(index(0), index(count() - 1));
    emit runningChanged();
}

void QueueModel::update_results(const std::vector<render::QueueJob::ItemResult>& results, int current) {
    bool any = false;
    for (size_t i = 0; i < items_.size(); ++i) {
        std::string st = items_[i].status;
        if (i < results.size() && results[i].state != render::JobState::Idle) {
            st = results[i].state == render::JobState::Succeeded ? "succeeded"
                 : results[i].state == render::JobState::Failed  ? "failed"
                 : results[i].state == render::JobState::Cancelled ? "cancelled"
                                                                   : st;
            items_[i].error = results[i].error.substr(0, results[i].error.find('\n'));
            items_[i].seconds = results[i].seconds;
        }
        if (static_cast<int>(i) == current && (i >= results.size() || results[i].state == render::JobState::Idle)) st = "running";
        if (st != items_[i].status) {
            items_[i].status = st;
            any = true;
        }
    }
    if (any) emit dataChanged(index(0), index(count() - 1));
}

void QueueModel::finish(const std::vector<render::QueueJob::ItemResult>& results) {
    update_results(results, -1);
    beginResetModel();
    std::vector<Item> left;
    for (size_t i = 0; i < items_.size(); ++i)
        if (i >= results.size() || results[i].state != render::JobState::Succeeded) left.push_back(std::move(items_[i]));
    items_ = std::move(left);
    endResetModel();
    editing_ = -1;
    save();
    emit countChanged();
    emit editingChanged();
}

} // namespace gmdr::qt
