// =============================================================================
//  log_model.hpp — журнал для QML (синглтон Log): рядки журналу ядра з будь-якого
//  потоку збираються й додаються до списку таймером (не щокадру). Налагоджувальні
//  рядки — лише коли їх увімкнено. Файл gmdr_log.txt пише main_qt.cpp.
// =============================================================================
#pragma once

#include <QAbstractListModel>
#include <QTimer>

#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "core/util/log.hpp"

namespace gmdr::qt {

class LogModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(bool showDebug READ showDebug WRITE setShowDebug NOTIFY filterChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QString lastWarning READ lastWarning NOTIFY lastWarningChanged)

public:
    enum Role { TimeRole = Qt::UserRole + 1, LevelRole, TextRole };

    explicit LogModel(QObject* parent = nullptr);
    ~LogModel() override;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool    showDebug() const { return show_debug_; }
    void    setShowDebug(bool on);
    int     count() const { return static_cast<int>(view_.size()); }
    QString lastWarning() const { return last_warning_; }

    Q_INVOKABLE QString allText() const;
    Q_INVOKABLE void    clear();

signals:
    void filterChanged();
    void countChanged();
    void lastWarningChanged();

private:
    struct Line {
        LogLevel    level;
        std::string time, text;
    };
    void flush();
    void rebuild();

    int               sink_ = 0;
    std::mutex        m_;
    std::vector<Line> pending_;
    std::deque<Line>  lines_;
    std::deque<size_t> view_;   // індекси в lines_ з урахуванням фільтра (зсуваються при обрізанні)
    size_t            dropped_ = 0;   // скільки рядків обрізано з початку
    bool              show_debug_ = false;
    QString           last_warning_;
    QTimer            timer_;
};

} // namespace gmdr::qt
