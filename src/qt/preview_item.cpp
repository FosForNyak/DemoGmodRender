#include "preview_item.hpp"

#include "job_service.hpp"

#include <QPainter>

namespace gmdr::qt {

JobService* PreviewItem::s_jobs = nullptr;

PreviewItem::PreviewItem(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setOpaquePainting(false);
    if (s_jobs)
        connect(s_jobs, &JobService::previewChanged, this, [this] {
            update();
            emit frameChanged();
        });
}

bool PreviewItem::hasFrame() const { return s_jobs && !s_jobs->preview_image().isNull(); }

void PreviewItem::setBackground(const QColor& c) {
    bg_ = c;
    update();
}

void PreviewItem::paint(QPainter* p) {
    p->fillRect(boundingRect(), bg_);
    if (!hasFrame()) return;
    const QImage& img = s_jobs->preview_image();
    const qreal k = std::min(width() / img.width(), height() / img.height());
    const QSizeF sz(img.width() * k, img.height() * k);
    const QRectF dst((width() - sz.width()) / 2, (height() - sz.height()) / 2, sz.width(), sz.height());
    p->setRenderHint(QPainter::SmoothPixmapTransform);
    p->drawImage(dst, img);
}

} // namespace gmdr::qt
