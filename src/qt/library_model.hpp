// =============================================================================
//  library_model.hpp — бібліотека демо для QML (синглтон Library): усі .dem з теки
//  гри і тек користувача; пошук, сортування, фільтр. Огляд тек — у фоні.
// =============================================================================
#pragma once

#include <QAbstractListModel>
#include <QTimer>

#include <future>
#include <string>
#include <vector>

#include "core/demo/library.hpp"

namespace gmdr::qt {

class ConfigModel;
class EnvService;

class LibraryModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(int total READ total NOTIFY listChanged)
    Q_PROPERTY(int count READ count NOTIFY listChanged)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY listChanged)
    Q_PROPERTY(QString sortKey READ sortKey WRITE setSortKey NOTIFY listChanged)
    Q_PROPERTY(bool sortDescending READ sortDescending WRITE setSortDescending NOTIFY listChanged)
    Q_PROPERTY(bool hideBroken READ hideBroken WRITE setHideBroken NOTIFY listChanged)
    Q_PROPERTY(QStringList folders READ folders NOTIFY foldersChanged)

public:
    enum Role { NameRole = Qt::UserRole + 1, PathRole, FolderRole, SizeRole, ModifiedRole, MapRole, ServerRole, RecordedByRole,
                SecondsRole, ErrorRole, CurrentRole };

    LibraryModel(ConfigModel* config, EnvService* env, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool        scanning() const { return future_.valid(); }
    int         total() const { return static_cast<int>(all_.size()); }
    int         count() const { return static_cast<int>(view_.size()); }
    QString     query() const { return query_; }
    void        setQuery(const QString& q);
    QString     sortKey() const { return sort_key_; }
    void        setSortKey(const QString& k);
    bool        sortDescending() const { return sort_desc_; }
    void        setSortDescending(bool d);
    bool        hideBroken() const { return hide_broken_; }
    void        setHideBroken(bool on);
    QStringList folders() const;

    Q_INVOKABLE void rescan();
    Q_INVOKABLE void addFolder(const QString& folder);
    Q_INVOKABLE void removeFolder(const QString& folder);
    Q_INVOKABLE QString pathAt(int row) const;

signals:
    void scanningChanged();
    void listChanged();
    void foldersChanged();

private:
    void rebuild();
    std::vector<std::string> dirs() const;

    ConfigModel*                                 config_;
    EnvService*                                  env_;
    std::vector<demo::LibraryEntry>              all_;
    std::vector<size_t>                          view_;
    std::future<std::vector<demo::LibraryEntry>> future_;
    QTimer                                       poll_;
    QString                                      query_, sort_key_ = "modified";
    std::string                                  last_dirs_;
    bool                                         sort_desc_ = true, hide_broken_ = false, scanned_ = false;
};

} // namespace gmdr::qt
