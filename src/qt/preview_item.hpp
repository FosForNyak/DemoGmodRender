// =============================================================================
//  preview_item.hpp — живе прев'ю кадрів рендеру (Preview у QML): останній кадр,
//  що пішов у кодер, вписаний у свою область зі збереженням пропорцій. Кадр
//  копіюється лише коли з'являється новий (JobService::previewChanged).
// =============================================================================
#pragma once

#include <QColor>
#include <QQuickPaintedItem>

namespace gmdr::qt {

class JobService;

class PreviewItem : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(bool hasFrame READ hasFrame NOTIFY frameChanged)
    Q_PROPERTY(QColor background READ background WRITE setBackground NOTIFY frameChanged)

public:
    explicit PreviewItem(QQuickItem* parent = nullptr);
    static void set_source(JobService* jobs) { s_jobs = jobs; }

    void   paint(QPainter* p) override;
    bool   hasFrame() const;
    QColor background() const { return bg_; }
    void   setBackground(const QColor& c);

signals:
    void frameChanged();

private:
    static JobService* s_jobs;
    QColor             bg_ = Qt::black;
};

} // namespace gmdr::qt
