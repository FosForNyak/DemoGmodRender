#include "timeline_item.hpp"

#include "project_service.hpp"
#include "qt_convert.hpp"

#include <QCursor>
#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include "core/util/i18n.hpp"
#include "core/util/strings.hpp"

#include <algorithm>
#include <cmath>

namespace gmdr::qt {

ProjectService* TimelineItem::s_project = nullptr;

namespace {
// Крок поділок лінійки: найменший із «круглих», за якого підписи не налазять
double tick_step(double seconds_per_px, double min_px) {
    static const double steps[] = {0.1, 0.2, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600, 7200};
    for (double s : steps)
        if (s / seconds_per_px >= min_px) return s;
    return 14400;
}

QString short_time(double t) {
    const int total = static_cast<int>(std::floor(t + 1e-6));
    const int h = total / 3600, m = (total / 60) % 60, s = total % 60;
    const double frac = t - total;
    QString out = h > 0 ? QString::asprintf("%d:%02d:%02d", h, m, s) : QString::asprintf("%d:%02d", m, s);
    if (frac > 0.049) out += QString::asprintf(".%d", static_cast<int>(std::lround(frac * 10)) % 10);
    return out;
}
} // namespace

TimelineItem::TimelineItem(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
    setAcceptHoverEvents(true);
    setActiveFocusOnTab(true);   // клавіші + / − (масштаб), коли шкала у фокусі
    lane_colors_ = {QColor(98, 135, 209), QColor(35, 170, 150), QColor(190, 130, 210), QColor(214, 150, 70),
                    QColor(90, 170, 90), QColor(210, 100, 120), QColor(120, 160, 220), QColor(180, 180, 90)};
    auto repaint = [this] { update(); };
    if (s_project) {
        connect(s_project, &ProjectService::changed, this, [this] {
            v0_ = 0;
            v1_ = duration();
            emit viewChanged();
            emit lanesChanged();
            update();
        });
        connect(s_project, &ProjectService::playersChanged, this, repaint);
        connect(s_project, &ProjectService::markersChanged, this, repaint);
        connect(s_project, &ProjectService::fragmentChanged, this, repaint);
        connect(s_project, &ProjectService::playheadChanged, this, repaint);
        v1_ = duration();
    }
    connect(this, &TimelineItem::styleChanged, this, repaint);
}

double TimelineItem::duration() const { return s_project ? s_project->duration() : 0.0; }
int    TimelineItem::laneCount() const { return s_project ? static_cast<int>(s_project->lanes().size()) : 0; }

void TimelineItem::clamp_view() {
    const double dur = duration();
    if (dur <= 0) {
        v0_ = v1_ = 0;
        return;
    }
    const double min_span = std::min(dur, 0.5);
    double span = std::clamp(v1_ - v0_, min_span, dur);
    if (!(span > 0)) span = dur;
    v0_ = std::clamp(v0_, 0.0, dur - span);
    v1_ = v0_ + span;
}

void TimelineItem::setViewStart(double t) {
    const double span = v1_ - v0_;
    v0_ = t;
    v1_ = t + span;
    clamp_view();
    emit viewChanged();
    update();
}

void TimelineItem::setViewEnd(double t) {
    v1_ = t;
    clamp_view();
    emit viewChanged();
    update();
}

void TimelineItem::setLaneHeight(qreal h) {
    lane_h_ = h;
    emit styleChanged();
}
void TimelineItem::setRulerHeight(qreal h) {
    ruler_h_ = h;
    emit styleChanged();
}
void TimelineItem::setFontPixelSize(qreal px) {
    font_px_ = px;
    emit styleChanged();
}
void TimelineItem::setLaneOffset(qreal y) {
    if (std::abs(y - lane_offset_) < 0.01) return;
    lane_offset_ = y;
    emit laneOffsetChanged();
    update();
}

double TimelineItem::timeAt(qreal x) const {
    if (width() <= 0) return v0_;
    return v0_ + (v1_ - v0_) * std::clamp(x / width(), 0.0, 1.0);
}

qreal TimelineItem::xAt(double t) const {
    if (v1_ <= v0_) return 0;
    return (t - v0_) / (v1_ - v0_) * width();
}

void TimelineItem::zoomAt(double t, double factor) {
    const double dur = duration();
    if (dur <= 0) return;
    const double span = std::clamp((v1_ - v0_) * factor, std::min(dur, 0.5), dur);
    const double k = v1_ > v0_ ? (t - v0_) / (v1_ - v0_) : 0.5;
    v0_ = t - span * k;
    v1_ = v0_ + span;
    clamp_view();
    emit viewChanged();
    update();
}

void TimelineItem::showAll() {
    v0_ = 0;
    v1_ = duration();
    emit viewChanged();
    update();
}

void TimelineItem::showRange(double from, double to) {
    const double pad = (to - from) * 0.05;
    v0_ = from - pad;
    v1_ = to + pad;
    clamp_view();
    emit viewChanged();
    update();
}

void TimelineItem::ensureVisible(double t) {
    if (t >= v0_ && t <= v1_) return;
    const double span = v1_ - v0_;
    v0_ = t - span * 0.2;
    v1_ = v0_ + span;
    clamp_view();
    emit viewChanged();
    update();
}

int TimelineItem::marker_near(qreal x) const {
    if (!s_project) return -1;
    const auto& list = s_project->marker_list();
    const double ti = s_project->tickInterval();
    int best = -1;
    qreal best_d = 6;
    for (size_t i = 0; i < list.size(); ++i) {
        const qreal d = std::abs(xAt(list[i].tick * ti) - x);
        if (d < best_d) {
            best_d = d;
            best = static_cast<int>(i);
        }
    }
    return best;
}

TimelineItem::Drag TimelineItem::edge_near(qreal x) const {
    if (!s_project || s_project->wholeDemo()) return Drag::None;
    if (std::abs(xAt(s_project->fragmentStart()) - x) < 5) return Drag::In;
    if (std::abs(xAt(s_project->fragmentEnd()) - x) < 5) return Drag::Out;
    return Drag::None;
}

void TimelineItem::paint(QPainter* p) {
    p->setRenderHint(QPainter::Antialiasing);
    const qreal W = width(), H = height();
    p->fillRect(QRectF(0, 0, W, H), bg_);
    if (!s_project || !s_project->loaded() || v1_ <= v0_) return;
    QFont font = p->font();
    font.setPixelSize(static_cast<int>(font_px_));
    p->setFont(font);
    const QFontMetricsF fm(font);

    const double spp = (v1_ - v0_) / std::max<qreal>(1, W);
    const qreal lanes_top = ruler_h_;
    const qreal chat_h = chat_row_h();
    const qreal lanes_bottom = H - chat_h;
    const auto& lanes = s_project->lanes();

    // ---- Доріжки гравців ----
    p->save();
    p->setClipRect(QRectF(0, lanes_top, W, lanes_bottom - lanes_top));
    for (size_t i = 0; i < lanes.size(); ++i) {
        const qreal y = lanes_top + static_cast<qreal>(i) * lane_h_ - lane_offset_;
        if (y + lane_h_ < lanes_top || y > lanes_bottom) continue;
        p->fillRect(QRectF(0, y, W, lane_h_ - 1), i % 2 ? lane_ : lane_.lighter(104));
        const bool muted = s_project->player_muted(lanes[i].key);
        QColor c = muted ? muted_ : lane_colors_.value(static_cast<int>(i % std::max<qsizetype>(1, lane_colors_.size()))).value<QColor>();
        p->setPen(Qt::NoPen);
        p->setBrush(c);
        const qreal pad = std::max<qreal>(3, lane_h_ * 0.2);
        for (const auto& [a, b] : lanes[i].spans) {
            if (b < v0_ || a > v1_) continue;
            const qreal xa = std::max<qreal>(0, xAt(a)), xb = std::min(W, std::max(xAt(a) + 1.5, xAt(b)));
            p->drawRoundedRect(QRectF(xa, y + pad, xb - xa, lane_h_ - 1 - pad * 2), 2, 2);
        }
    }
    if (lanes.empty()) {
        p->setPen(dim_);
        p->drawText(QRectF(8, lanes_top, W - 16, lane_h_), Qt::AlignVCenter | Qt::AlignLeft,
                    qs(gmdr::tr("Голосу в демо немає — шкала показує лише фрагмент")));
    }
    p->restore();

    // ---- Доріжка чату ----
    {
        const qreal y = lanes_bottom;
        p->fillRect(QRectF(0, y, W, chat_h), lane_.darker(108));
        p->setPen(QPen(grid_, 1));
        p->drawLine(QPointF(0, y), QPointF(W, y));
        if (auto a = s_project->analysis()) {
            const double ti = a->tick_interval;
            QColor cc = text_;
            cc.setAlpha(150);
            p->setPen(QPen(cc, 1.5));
            for (const auto& e : a->events) {
                if (e.kind != demo::DemoEventKind::Chat) continue;
                const double t = e.tick * ti;
                if (t < v0_ || t > v1_) continue;
                const qreal x = xAt(t);
                p->drawLine(QPointF(x, y + chat_h * 0.25), QPointF(x, y + chat_h * 0.75));
            }
        }
    }

    // ---- Фрагмент: поза ним — затемнено, межі — лінії ----
    const double in_t = s_project->fragmentStart(), out_t = s_project->fragmentEnd();
    if (!s_project->wholeDemo()) {
        QColor shade = bg_;
        shade.setAlpha(150);
        const qreal xi = xAt(in_t), xo = xAt(out_t);
        if (xi > 0) p->fillRect(QRectF(0, lanes_top, std::min(W, xi), H - lanes_top), shade);
        if (xo < W) p->fillRect(QRectF(std::max<qreal>(0, xo), lanes_top, W - std::max<qreal>(0, xo), H - lanes_top), shade);
        p->setPen(QPen(accent_, 2));
        for (qreal x : {xi, xo})
            if (x >= -2 && x <= W + 2) p->drawLine(QPointF(x, 0), QPointF(x, H));
    }

    // ---- Лінійка ----
    p->fillRect(QRectF(0, 0, W, ruler_h_), bg_.lighter(112));
    {
        QColor frag = accent_;
        frag.setAlpha(70);
        const qreal xi = std::max<qreal>(0, xAt(in_t)), xo = std::min(W, xAt(out_t));
        if (xo > xi) p->fillRect(QRectF(xi, ruler_h_ - 5, xo - xi, 5), frag);
    }
    const double step = tick_step(spp, fm.horizontalAdvance("00:00:00") + 18);
    const double minor = step / (step >= 60 ? 6 : step >= 10 ? 5 : step >= 1 ? 4 : 2);
    p->setPen(QPen(grid_, 1));
    for (double t = std::floor(v0_ / minor) * minor; t <= v1_; t += minor) {
        const qreal x = xAt(t);
        p->drawLine(QPointF(x, ruler_h_ - 4), QPointF(x, ruler_h_));
    }
    for (double t = std::floor(v0_ / step) * step; t <= v1_; t += step) {
        const qreal x = xAt(t);
        p->setPen(QPen(grid_, 1));
        p->drawLine(QPointF(x, ruler_h_ * 0.45), QPointF(x, ruler_h_));
        QColor g = grid_;
        g.setAlpha(60);
        p->setPen(QPen(g, 1));
        p->drawLine(QPointF(x, ruler_h_), QPointF(x, lanes_bottom));
        p->setPen(dim_);
        p->drawText(QPointF(x + 4, fm.ascent() + 3), short_time(t));
    }
    p->setPen(QPen(grid_, 1));
    p->drawLine(QPointF(0, ruler_h_ - 0.5), QPointF(W, ruler_h_ - 0.5));

    // ---- Позначки ----
    {
        const auto& list = s_project->marker_list();
        const double ti = s_project->tickInterval();
        for (size_t i = 0; i < list.size(); ++i) {
            const double t = drag_ == Drag::Marker && static_cast<int>(i) == drag_marker_ ? drag_to_ : list[i].tick * ti;
            if (t < v0_ || t > v1_) continue;
            const qreal x = xAt(t);
            QColor line = marker_;
            line.setAlpha(140);
            QPen pen(line, 1, Qt::DashLine);
            p->setPen(pen);
            p->drawLine(QPointF(x, ruler_h_), QPointF(x, H));
            QPainterPath flag;
            const qreal fw = 7, fh = ruler_h_ * 0.55;
            flag.moveTo(x - fw / 2, 1);
            flag.lineTo(x + fw / 2, 1);
            flag.lineTo(x + fw / 2, fh - 3);
            flag.lineTo(x, fh);
            flag.lineTo(x - fw / 2, fh - 3);
            flag.closeSubpath();
            p->setPen(Qt::NoPen);
            p->setBrush(marker_);
            p->drawPath(flag);
            if (!list[i].title.empty() && (v1_ - v0_) / std::max<qreal>(1, W) < 0.5) {
                p->setPen(marker_);
                p->drawText(QPointF(x + 6, fh), fm.elidedText(qs(list[i].title), Qt::ElideRight, 140));
            }
        }
    }

    // ---- Вибір відрізка мишею ----
    if (drag_ == Drag::Select) {
        QColor sel = accent_;
        sel.setAlpha(55);
        const qreal a = xAt(std::min(drag_from_, drag_to_)), b = xAt(std::max(drag_from_, drag_to_));
        p->fillRect(QRectF(a, ruler_h_, b - a, H - ruler_h_), sel);
    }
    if (drag_ == Drag::In || drag_ == Drag::Out) {
        p->setPen(QPen(text_, 2, Qt::DashLine));
        const qreal x = xAt(drag_to_);
        p->drawLine(QPointF(x, 0), QPointF(x, H));
    }

    // ---- Курсор ----
    const double ph = s_project->playhead();
    if (ph >= v0_ && ph <= v1_) {
        const qreal x = xAt(ph);
        p->setPen(QPen(accent_, 1.5));
        p->drawLine(QPointF(x, ruler_h_ * 0.5), QPointF(x, H));
        QPainterPath head;
        head.moveTo(x - 6, 0);
        head.lineTo(x + 6, 0);
        head.lineTo(x + 6, ruler_h_ * 0.35);
        head.lineTo(x, ruler_h_ * 0.6);
        head.lineTo(x - 6, ruler_h_ * 0.35);
        head.closeSubpath();
        p->setPen(Qt::NoPen);
        p->setBrush(accent_);
        p->drawPath(head);
    }
}

void TimelineItem::mousePressEvent(QMouseEvent* e) {
    if (!s_project || !s_project->loaded() || !interactive_) {
        e->ignore();
        return;
    }
    forceActiveFocus();
    const QPointF pos = e->position();
    const double t = timeAt(pos.x());
    if (e->button() == Qt::RightButton) {
        emit contextRequested(t, pos.x(), pos.y(), marker_near(pos.x()));
        return;
    }
    press_x_ = pos.x();
    const int mk = pos.y() < ruler_h_ ? marker_near(pos.x()) : -1;
    const Drag edge = edge_near(pos.x());
    if (mk >= 0) {
        drag_ = Drag::Marker;
        drag_marker_ = mk;
        drag_to_ = s_project->marker_list()[static_cast<size_t>(mk)].tick * s_project->tickInterval();
    } else if (edge != Drag::None) {
        drag_ = edge;
        drag_to_ = edge == Drag::In ? s_project->fragmentStart() : s_project->fragmentEnd();
    } else if (pos.y() < ruler_h_) {
        drag_ = Drag::Seek;
        s_project->setPlayhead(t);
    } else {
        drag_ = Drag::Select;
        drag_from_ = drag_to_ = t;
    }
    update();
}

void TimelineItem::mouseMoveEvent(QMouseEvent* e) {
    const double t = std::clamp(timeAt(e->position().x()), 0.0, duration());
    switch (drag_) {
    case Drag::Seek: s_project->setPlayhead(t); break;
    case Drag::Select:
    case Drag::In:
    case Drag::Out:
    case Drag::Marker:
        drag_to_ = t;
        update();
        break;
    default: break;
    }
}

void TimelineItem::mouseReleaseEvent(QMouseEvent* e) {
    const Drag d = drag_;
    drag_ = Drag::None;
    const bool moved = std::abs(e->position().x() - press_x_) > 4;
    switch (d) {
    case Drag::Select:
        if (moved && std::abs(drag_to_ - drag_from_) > 0.05) emit rangeSelected(std::min(drag_from_, drag_to_), std::max(drag_from_, drag_to_));
        else s_project->setPlayhead(drag_from_);
        break;
    case Drag::In:
        if (moved) s_project->setFragmentStart(drag_to_);
        break;
    case Drag::Out:
        if (moved) s_project->setFragmentEnd(drag_to_);
        break;
    case Drag::Marker:
        if (moved) s_project->moveMarker(drag_marker_, drag_to_);
        else s_project->setPlayhead(drag_to_);
        break;
    default: break;
    }
    drag_marker_ = -1;
    update();
}

void TimelineItem::mouseDoubleClickEvent(QMouseEvent* e) {
    const int mk = marker_near(e->position().x());
    if (mk >= 0) emit markerActivated(mk);
}

void TimelineItem::wheelEvent(QWheelEvent* e) {
    if (!s_project || !s_project->loaded()) {
        e->ignore();
        return;
    }
    const QPoint d = e->angleDelta();
    const double t = timeAt(e->position().x());
    if (e->modifiers() & Qt::ShiftModifier || d.x() != 0) {
        const int steps = d.x() != 0 ? d.x() : d.y();
        const double span = v1_ - v0_;
        setViewStart(v0_ - span * 0.1 * steps / 120.0);
    } else if (e->modifiers() & Qt::ControlModifier) {
        setLaneOffset(std::max<qreal>(0, lane_offset_ - d.y() / 120.0 * lane_h_));
    } else {
        zoomAt(t, std::pow(0.85, d.y() / 120.0));
    }
    e->accept();
}

void TimelineItem::hoverMoveEvent(QHoverEvent* e) {
    const QPointF pos = e->position();
    const bool edge = edge_near(pos.x()) != Drag::None || (pos.y() < ruler_h_ && marker_near(pos.x()) >= 0);
    setCursor(edge ? Qt::SizeHorCursor : Qt::ArrowCursor);
}

} // namespace gmdr::qt
