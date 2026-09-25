// =============================================================================
//  queue_model.hpp — черга рендерів для QML (синглтон Queue): список наборів
//  налаштувань (gmdr_queue.json, як і раніше). Кожен пункт перевіряється тими
//  самими правилами, що й перед виконанням (мета «пункт черги»), — у списку видно
//  помилки й попередження ще до запуску. Пункт можна відкрити для редагування
//  (налаштування йдуть у робочий простір «Рендер») і зберегти назад.
// =============================================================================
#pragma once

#include <QAbstractListModel>

#include <string>
#include <vector>

#include "core/config/constraints.hpp"
#include "core/render/jobs.hpp"

namespace gmdr::qt {

class ConfigModel;
class ProjectService;

class QueueModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(int editingIndex READ editingIndex NOTIFY editingChanged)
    Q_PROPERTY(int errorItems READ errorItems NOTIFY countChanged)

public:
    enum Role {
        DemoRole = Qt::UserRole + 1, DemoNameRole, OutputRole, RendererRole, ResolutionRole, FpsRole, CodecRole,
        ParallelRole, FragmentRole, StatusRole, ErrorRole, SecondsRole, ErrorsRole, WarningsRole, IssuesRole, SummaryRole,
    };

    QueueModel(ConfigModel* config, ProjectService* project, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int  count() const { return static_cast<int>(items_.size()); }
    bool running() const { return running_; }
    int  editingIndex() const { return editing_; }
    int  errorItems() const;

    std::vector<render::RenderSettings> settings_list() const;
    void set_running(bool on);
    void update_results(const std::vector<render::QueueJob::ItemResult>& results, int current);
    void finish(const std::vector<render::QueueJob::ItemResult>& results);   // готові прибираються
    void revalidate();

    Q_INVOKABLE bool addCurrent();                        // поточні налаштування (фрагмент демо)
    Q_INVOKABLE bool addDemo(const QString& path);        // ціле демо з поточними налаштуваннями
    Q_INVOKABLE void remove(int index);
    Q_INVOKABLE void move(int from, int to);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void edit(int index);                     // відкрити пункт у робочому просторі «Рендер»
    Q_INVOKABLE void saveEdit();                          // зберегти поточні налаштування в пункт
    Q_INVOKABLE void cancelEdit();

signals:
    void countChanged();
    void runningChanged();
    void editingChanged();

private:
    struct Item {
        render::RenderSettings   s;
        double                   tick_interval = 0;
        config::ValidationResult check;
        std::string              status = "pending";   // pending / running / succeeded / failed / cancelled
        std::string              error;
        double                   seconds = 0;
    };
    void load();
    void save() const;
    void push(Item it);
    void validate(Item& it) const;

    ConfigModel*      config_;
    ProjectService*   project_;
    std::vector<Item> items_;
    bool              running_ = false;
    int               editing_ = -1;
};

} // namespace gmdr::qt
