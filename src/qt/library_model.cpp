#include "library_model.hpp"

#include "config_model.hpp"
#include "env_service.hpp"
#include "qt_convert.hpp"

#include "core/game/game_renderer.hpp"
#include "core/util/strings.hpp"

#include <QUrl>

#include <algorithm>
#include <filesystem>

namespace gmdr::qt {

namespace fs = std::filesystem;

LibraryModel::LibraryModel(ConfigModel* config, EnvService* env, QObject* parent)
    : QAbstractListModel(parent), config_(config), env_(env) {
    poll_.setInterval(150);
    connect(&poll_, &QTimer::timeout, this, [this] {
        if (!future_.valid() || future_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        poll_.stop();
        all_ = future_.get();
        scanned_ = true;
        rebuild();
        emit scanningChanged();
    });
    // Гру знайдено пізніше (огляд середовища у фоні) — її тека теж у бібліотеці
    connect(env_, &EnvService::changed, this, [this] {
        std::string now;
        for (const auto& d : dirs()) now += d + ";";
        if (now == last_dirs_) return;
        last_dirs_ = now;
        if (scanned_) rescan();
        emit foldersChanged();
    });
}

std::vector<std::string> LibraryModel::dirs() const {
    std::vector<std::string> out;
    if (const auto* g = env_->env().game.find(render::renderer_of(config_->settings()).id()); g && g->install.ok())
        out.push_back(path_to_utf8(path_from_utf8(g->install.detail) / "garrysmod"));
    for (const auto& d : split(config_->settings().library_dirs, ';'))
        if (!trim(d).empty()) out.push_back(trim(d));
    return out;
}

QStringList LibraryModel::folders() const { return qs_list(dirs()); }

void LibraryModel::rescan() {
    if (future_.valid()) return;
    std::vector<fs::path> paths;
    for (const auto& d : dirs()) paths.push_back(path_from_utf8(d));
    future_ = std::async(std::launch::async, [paths] { return demo::scan_demo_library(paths); });
    poll_.start();
    emit scanningChanged();
}

void LibraryModel::addFolder(const QString& qfolder) {
    std::string f = ss(qfolder);
    if (f.rfind("file:", 0) == 0) f = ss(QUrl(qfolder).toLocalFile());
    if (f.empty()) return;
    std::string list = config_->settings().library_dirs;
    for (const auto& d : split(list, ';'))
        if (trim(d) == f) return;
    list += (list.empty() ? "" : ";") + f;
    config_->set("library_dirs", qs(list));
    emit foldersChanged();
    rescan();
}

void LibraryModel::removeFolder(const QString& qfolder) {
    const std::string f = ss(qfolder);
    std::vector<std::string> keep;
    for (const auto& d : split(config_->settings().library_dirs, ';'))
        if (!trim(d).empty() && trim(d) != f) keep.push_back(trim(d));
    config_->set("library_dirs", qs(join(keep, ";")));
    emit foldersChanged();
    rescan();
}

void LibraryModel::setQuery(const QString& q) {
    if (q == query_) return;
    query_ = q;
    rebuild();
}

void LibraryModel::setSortKey(const QString& k) {
    if (k == sort_key_) return;
    sort_key_ = k;
    rebuild();
}

void LibraryModel::setSortDescending(bool d) {
    if (d == sort_desc_) return;
    sort_desc_ = d;
    rebuild();
}

void LibraryModel::setHideBroken(bool on) {
    if (on == hide_broken_) return;
    hide_broken_ = on;
    rebuild();
}

void LibraryModel::rebuild() {
    beginResetModel();
    view_.clear();
    const std::string q = ss(query_);
    for (size_t i = 0; i < all_.size(); ++i) {
        if (hide_broken_ && !all_[i].error.empty()) continue;
        if (!q.empty() && !demo::library_match(all_[i], q)) continue;
        view_.push_back(i);
    }
    const std::string key = ss(sort_key_);
    // -1 / 0 / 1: порівняння за вибраним стовпцем
    auto cmp = [&](const demo::LibraryEntry& x, const demo::LibraryEntry& y) {
        auto three = [](const auto& a, const auto& b) { return a < b ? -1 : b < a ? 1 : 0; };
        if (key == "name") return three(to_lower(x.name), to_lower(y.name));
        if (key == "map") return three(to_lower(x.map), to_lower(y.map));
        if (key == "size") return three(x.size, y.size);
        if (key == "duration") return three(x.seconds, y.seconds);
        return three(x.modified, y.modified);
    };
    std::stable_sort(view_.begin(), view_.end(), [&](size_t a, size_t b) {
        const int c = cmp(all_[a], all_[b]);
        return sort_desc_ ? c > 0 : c < 0;
    });
    endResetModel();
    emit listChanged();
}

int LibraryModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : count(); }

QHash<int, QByteArray> LibraryModel::roleNames() const {
    return {{NameRole, "name"},     {PathRole, "path"},     {FolderRole, "folder"},       {SizeRole, "size"},
            {ModifiedRole, "modified"}, {MapRole, "map"},   {ServerRole, "server"},       {RecordedByRole, "recordedBy"},
            {SecondsRole, "seconds"}, {ErrorRole, "error"}, {CurrentRole, "current"}};
}

QVariant LibraryModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= count()) return {};
    const auto& e = all_[view_[static_cast<size_t>(index.row())]];
    switch (role) {
    case NameRole: return qs(e.name);
    case PathRole: return qs(e.path);
    case FolderRole: return qs(e.folder);
    case SizeRole: return static_cast<double>(e.size);
    case ModifiedRole: return static_cast<double>(e.modified);
    case MapRole: return qs(e.map);
    case ServerRole: return qs(e.server);
    case RecordedByRole: return qs(e.recorded_by);
    case SecondsRole: return e.seconds;
    case ErrorRole: return qs(e.error);
    case CurrentRole: return iequals(e.path, config_->settings().demo_path);
    default: return {};
    }
}

QString LibraryModel::pathAt(int row) const {
    if (row < 0 || row >= count()) return {};
    return qs(all_[view_[static_cast<size_t>(row)]].path);
}

} // namespace gmdr::qt
