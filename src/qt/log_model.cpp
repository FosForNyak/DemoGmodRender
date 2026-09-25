#include "log_model.hpp"

#include "qt_convert.hpp"

#include <ctime>

namespace gmdr::qt {

namespace {
constexpr size_t kMaxLines = 5000;

std::string now_time() {
    const std::time_t t = std::time(nullptr);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&t));
    return buf;
}
} // namespace

LogModel::LogModel(QObject* parent) : QAbstractListModel(parent) {
    sink_ = add_log_sink([this](LogLevel l, const std::string& text) {
        std::lock_guard lock(m_);
        pending_.push_back({l, now_time(), text});
    });
    timer_.setInterval(200);
    connect(&timer_, &QTimer::timeout, this, &LogModel::flush);
    timer_.start();
}

LogModel::~LogModel() {
    if (sink_) remove_log_sink(sink_);
}

void LogModel::flush() {
    std::vector<Line> fresh;
    {
        std::lock_guard lock(m_);
        fresh.swap(pending_);
    }
    if (fresh.empty()) return;
    // Нові рядки, що проходять фільтр
    std::vector<Line> visible;
    for (const auto& l : fresh)
        if (show_debug_ || l.level != LogLevel::Debug) visible.push_back(l);
    for (const auto& l : fresh)
        if (l.level == LogLevel::Warn || l.level == LogLevel::Error) last_warning_ = qs(l.text);
    const size_t before = lines_.size();
    for (auto& l : fresh) lines_.push_back(std::move(l));
    // Нові рядки у видимому списку
    size_t idx = before;
    std::vector<size_t> add;
    for (size_t i = before; i < lines_.size(); ++i, ++idx)
        if (show_debug_ || lines_[i].level != LogLevel::Debug) add.push_back(i + dropped_);
    if (!add.empty()) {
        beginInsertRows({}, count(), count() + static_cast<int>(add.size()) - 1);
        for (size_t a : add) view_.push_back(a);
        endInsertRows();
    }
    // Обрізати найстаріші
    if (lines_.size() > kMaxLines) {
        const size_t drop = lines_.size() - kMaxLines;
        size_t view_drop = 0;
        while (view_drop < view_.size() && view_[view_drop] < dropped_ + drop) ++view_drop;
        if (view_drop > 0) {
            beginRemoveRows({}, 0, static_cast<int>(view_drop) - 1);
            view_.erase(view_.begin(), view_.begin() + static_cast<long>(view_drop));
            endRemoveRows();
        }
        lines_.erase(lines_.begin(), lines_.begin() + static_cast<long>(drop));
        dropped_ += drop;
    }
    emit countChanged();
    if (!last_warning_.isEmpty()) emit lastWarningChanged();
}

void LogModel::rebuild() {
    beginResetModel();
    view_.clear();
    for (size_t i = 0; i < lines_.size(); ++i)
        if (show_debug_ || lines_[i].level != LogLevel::Debug) view_.push_back(i + dropped_);
    endResetModel();
    emit countChanged();
}

void LogModel::setShowDebug(bool on) {
    if (on == show_debug_) return;
    show_debug_ = on;
    rebuild();
    emit filterChanged();
}

int LogModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : count(); }

QHash<int, QByteArray> LogModel::roleNames() const { return {{TimeRole, "time"}, {LevelRole, "level"}, {TextRole, "text"}}; }

QVariant LogModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= count()) return {};
    const size_t i = view_[static_cast<size_t>(index.row())] - dropped_;
    if (i >= lines_.size()) return {};
    const Line& l = lines_[i];
    switch (role) {
    case TimeRole: return qs(l.time);
    case LevelRole:   // стабільний ідентифікатор (назви рівнів у журналі перекладаються)
        return l.level == LogLevel::Error ? "error" : l.level == LogLevel::Warn ? "warning" : l.level == LogLevel::Debug ? "debug" : "info";
    case TextRole: return qs(l.text);
    default: return {};
    }
}

QString LogModel::allText() const {
    QString out;
    for (size_t v : view_) {
        const size_t i = v - dropped_;
        if (i >= lines_.size()) continue;
        out += qs(lines_[i].time) + " [" + qs(log_level_name(lines_[i].level)) + "] " + qs(lines_[i].text) + "\n";
    }
    return out;
}

void LogModel::clear() {
    beginResetModel();
    dropped_ += lines_.size();
    lines_.clear();
    view_.clear();
    endResetModel();
    emit countChanged();
}

} // namespace gmdr::qt
