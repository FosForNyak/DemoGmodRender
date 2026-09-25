#include "icon_item.hpp"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>

#include <cmath>
#include <initializer_list>
#include <numbers>

namespace gmdr::qt {

namespace {

constexpr float kPi = std::numbers::pi_v<float>;

// Малювання в координатах значка: центр c, пів-розміру h (як draw_icon у старому вікні)
struct Pen {
    QPainter& p;
    QPointF   c;
    float     h, t;
    QColor    col;

    QPointF at(float x, float y) const { return {c.x() + x * h, c.y() + y * h}; }
    void stroke(float w) const {
        QPen pen(col, w);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
    }
    void fill() const {
        p.setPen(Qt::NoPen);
        p.setBrush(col);
    }
    void line(float x0, float y0, float x1, float y1, float w) const {
        stroke(w);
        p.drawLine(at(x0, y0), at(x1, y1));
    }
    void poly(std::initializer_list<QPointF> pts, float w, bool closed = false) const {
        QPainterPath path;
        bool first = true;
        for (const auto& q : pts) {
            if (first) path.moveTo(at(q.x(), q.y()));
            else path.lineTo(at(q.x(), q.y()));
            first = false;
        }
        if (closed) path.closeSubpath();
        stroke(w);
        p.drawPath(path);
    }
    void polyfill(std::initializer_list<QPointF> pts) const {
        QPolygonF poly;
        for (const auto& q : pts) poly << at(q.x(), q.y());
        fill();
        p.drawPolygon(poly);
    }
    void circle(float x, float y, float r, float w) const {
        stroke(w);
        p.drawEllipse(at(x, y), r * h, r * h);
    }
    void dot(float x, float y, float r_px) const {
        fill();
        p.drawEllipse(at(x, y), r_px, r_px);
    }
    void disc(float x, float y, float r) const {
        fill();
        p.drawEllipse(at(x, y), r * h, r * h);
    }
    void rect(float x0, float y0, float x1, float y1, float round, float w) const {
        stroke(w);
        p.drawRoundedRect(QRectF(at(x0, y0), at(x1, y1)), round * h, round * h);
    }
    void rectfill(float x0, float y0, float x1, float y1, float round) const {
        fill();
        p.drawRoundedRect(QRectF(at(x0, y0), at(x1, y1)), round * h, round * h);
    }
    // Дуга (кути в радіанах, y вниз — як у ImGui PathArcTo)
    void arc(float cx, float cy, float r, float a0, float a1, float w) const {
        QPainterPath path;
        const int n = 24;
        for (int i = 0; i <= n; ++i) {
            const float a = a0 + (a1 - a0) * i / n;
            const QPointF q(c.x() + cx * h + std::cos(a) * r * h, c.y() + cy * h + std::sin(a) * r * h);
            if (i == 0) path.moveTo(q);
            else path.lineTo(q);
        }
        stroke(w);
        p.drawPath(path);
    }
};

} // namespace

IconItem::IconItem(QQuickItem* parent) : QQuickPaintedItem(parent) { setAntialiasing(true); }

void IconItem::setName(const QString& n) {
    if (n == name_) return;
    name_ = n;
    update();
    emit nameChanged();
}

void IconItem::setColor(const QColor& c) {
    if (c == color_) return;
    color_ = c;
    update();
    emit colorChanged();
}

void IconItem::paint(QPainter* painter) {
    painter->setRenderHint(QPainter::Antialiasing);
    const float size = static_cast<float>(std::min(width(), height()));
    const float h = size * 0.5f;
    const float t = std::max(1.0f, size * 0.09f);
    const Pen d{*painter, QPointF(width() / 2, height() / 2), h, t, color_};
    const QString& n = name_;
    if (n == "play") d.polyfill({{-0.55, -0.72}, {0.75, 0}, {-0.55, 0.72}});
    else if (n == "pause") {
        d.rectfill(-0.55f, -0.62f, -0.18f, 0.62f, 0.08f);
        d.rectfill(0.18f, -0.62f, 0.55f, 0.62f, 0.08f);
    } else if (n == "stop") d.rectfill(-0.55f, -0.55f, 0.55f, 0.55f, 0.12f);
    else if (n == "record") d.disc(0, 0, 0.6f);
    else if (n == "menu")
        for (int i = -1; i <= 1; ++i) d.line(-0.7f, i * 0.45f, 0.7f, i * 0.45f, t);
    else if (n == "chevronDown") d.poly({{-0.55, -0.28}, {0, 0.28}, {0.55, -0.28}}, t * 1.2f);
    else if (n == "chevronUp") d.poly({{-0.55, 0.28}, {0, -0.28}, {0.55, 0.28}}, t * 1.2f);
    else if (n == "chevronRight") d.poly({{-0.28, -0.55}, {0.28, 0}, {-0.28, 0.55}}, t * 1.2f);
    else if (n == "chevronLeft") d.poly({{0.28, -0.55}, {-0.28, 0}, {0.28, 0.55}}, t * 1.2f);
    else if (n == "folder") d.poly({{-0.8, -0.62}, {-0.2, -0.62}, {0.02, -0.38}, {0.8, -0.38}, {0.8, 0.62}, {-0.8, 0.62}}, t, true);
    else if (n == "plus") {
        d.line(-0.65f, 0, 0.65f, 0, t * 1.2f);
        d.line(0, -0.65f, 0, 0.65f, t * 1.2f);
    } else if (n == "minus") d.line(-0.65f, 0, 0.65f, 0, t * 1.2f);
    else if (n == "close") {
        d.line(-0.5f, -0.5f, 0.5f, 0.5f, t * 1.2f);
        d.line(0.5f, -0.5f, -0.5f, 0.5f, t * 1.2f);
    } else if (n == "search") {
        d.circle(-0.15f, -0.15f, 0.48f, t);
        d.line(0.2f, 0.2f, 0.7f, 0.7f, t * 1.3f);
    } else if (n == "markIn") d.poly({{0.35, -0.7}, {-0.2, -0.7}, {-0.2, 0.7}, {0.35, 0.7}}, t * 1.2f);
    else if (n == "markOut") d.poly({{-0.35, -0.7}, {0.2, -0.7}, {0.2, 0.7}, {-0.35, 0.7}}, t * 1.2f);
    else if (n == "goToIn") {
        d.line(-0.6f, -0.65f, -0.6f, 0.65f, t * 1.3f);
        d.polyfill({{0.65, -0.65}, {0.65, 0.65}, {-0.35, 0}});
    } else if (n == "goToOut") {
        d.line(0.6f, -0.65f, 0.6f, 0.65f, t * 1.3f);
        d.polyfill({{-0.65, -0.65}, {0.35, 0}, {-0.65, 0.65}});
    } else if (n == "marker") d.polyfill({{-0.5, -0.65}, {0.5, -0.65}, {0.5, 0.2}, {0, 0.7}, {-0.5, 0.2}});
    else if (n == "speaker") {
        d.polyfill({{-0.75, -0.25}, {-0.4, -0.25}, {0, -0.65}, {0, 0.65}, {-0.4, 0.25}, {-0.75, 0.25}});
        d.arc(0.05f, 0, 0.38f, -0.9f, 0.9f, t);
        d.arc(0.05f, 0, 0.7f, -0.9f, 0.9f, t);
    } else if (n == "speakerOff") {
        d.polyfill({{-0.75, -0.25}, {-0.4, -0.25}, {0, -0.65}, {0, 0.65}, {-0.4, 0.25}, {-0.75, 0.25}});
        d.line(0.22f, -0.3f, 0.78f, 0.3f, t);
        d.line(0.78f, -0.3f, 0.22f, 0.3f, t);
    } else if (n == "headphones") {
        d.arc(0, 0.1f, 0.62f, kPi, kPi * 2.0f, t * 1.2f);
        d.rectfill(-0.78f, 0.05f, -0.38f, 0.7f, 0.12f);
        d.rectfill(0.38f, 0.05f, 0.78f, 0.7f, 0.12f);
    } else if (n == "eye") {
        d.arc(0, 0.62f, 1.0f, -kPi * 0.8f, -kPi * 0.2f, t);
        d.arc(0, -0.62f, 1.0f, kPi * 0.2f, kPi * 0.8f, t);
        d.disc(0, 0, 0.26f);
    } else if (n == "check") d.poly({{-0.6, 0}, {-0.15, 0.45}, {0.65, -0.5}}, t * 1.4f);
    else if (n == "warning") {
        d.poly({{0, -0.72}, {0.78, 0.62}, {-0.78, 0.62}}, t, true);
        d.line(0, -0.22f, 0, 0.18f, t);
        d.dot(0, 0.38f, t * 0.7f);
    } else if (n == "error") {
        d.circle(0, 0, 0.72f, t);
        d.line(-0.3f, -0.3f, 0.3f, 0.3f, t);
        d.line(0.3f, -0.3f, -0.3f, 0.3f, t);
    } else if (n == "info") {
        d.circle(0, 0, 0.72f, t);
        d.dot(0, -0.32f, t * 0.75f);
        d.line(0, -0.08f, 0, 0.38f, t);
    } else if (n == "film") {
        d.rect(-0.8f, -0.6f, 0.8f, 0.6f, 0.1f, t);
        d.line(-0.45f, -0.6f, -0.45f, 0.6f, t);
        d.line(0.45f, -0.6f, 0.45f, 0.6f, t);
        for (float y : {-0.2f, 0.2f}) {
            d.line(-0.8f, y, -0.45f, y, t);
            d.line(0.45f, y, 0.8f, y, t);
        }
    } else if (n == "queue") {
        for (int i = -1; i <= 1; ++i) {
            const float y = i * 0.5f;
            d.rectfill(-0.78f, y - t / h, -0.5f, y + t / h, 0);
            d.line(-0.3f, y, 0.78f, y, t);
        }
    } else if (n == "pencil") {
        d.poly({{-0.62, 0.62}, {-0.62, 0.25}, {0.35, -0.72}, {0.72, -0.35}, {-0.25, 0.62}}, t, true);
        d.line(0.15f, -0.52f, 0.52f, -0.15f, t);
    } else if (n == "refresh") {
        d.arc(0, 0, 0.6f, -kPi * 0.3f, kPi * 1.35f, t);
        const float ex = std::cos(-kPi * 0.3f) * 0.6f, ey = std::sin(-kPi * 0.3f) * 0.6f;
        d.polyfill({{ex - 0.3, ey - 0.15}, {ex + 0.25, ey - 0.35}, {ex + 0.1, ey + 0.25}});
    } else if (n == "home") {
        d.poly({{-0.78, -0.02}, {0, -0.74}, {0.78, -0.02}}, t * 1.1f);
        d.poly({{-0.55, -0.2}, {-0.55, 0.7}, {0.55, 0.7}, {0.55, -0.2}}, t * 1.1f);
        d.rectfill(-0.16f, 0.22f, 0.16f, 0.7f, 0.05f);
    } else if (n == "globe") {
        d.circle(0, 0, 0.76f, t);
        d.stroke(t);
        painter->drawEllipse(d.at(0, 0), 0.34 * h, 0.76 * h);
        d.line(-0.76f, 0, 0.76f, 0, t);
        d.line(-0.64f, -0.38f, 0.64f, -0.38f, t * 0.8f);
        d.line(-0.64f, 0.38f, 0.64f, 0.38f, t * 0.8f);
    } else if (n == "gamepad") {
        d.rect(-0.85f, -0.45f, 0.85f, 0.5f, 0.42f, t);
        d.line(-0.55f, 0.02f, -0.15f, 0.02f, t);
        d.line(-0.35f, -0.18f, -0.35f, 0.22f, t);
        d.dot(0.3f, -0.04f, t * 0.95f);
        d.dot(0.52f, 0.16f, t * 0.95f);
    } else if (n == "scissors") {
        d.circle(-0.45f, 0.48f, 0.24f, t);
        d.circle(0.45f, 0.48f, 0.24f, t);
        d.line(-0.3f, 0.3f, 0.45f, -0.72f, t);
        d.line(0.3f, 0.3f, -0.45f, -0.72f, t);
    } else if (n == "chat") {
        d.rect(-0.78f, -0.62f, 0.78f, 0.36f, 0.24f, t);
        d.polyfill({{-0.42, 0.3}, {-0.1, 0.3}, {-0.5, 0.74}});
        for (float x : {-0.36f, 0.0f, 0.36f}) d.dot(x, -0.13f, t * 0.8f);
    } else if (n == "library") {
        d.rect(-0.72f, -0.62f, -0.36f, 0.66f, 0.06f, t);
        d.rect(-0.2f, -0.62f, 0.16f, 0.66f, 0.06f, t);
        d.poly({{0.32, -0.5}, {0.62, -0.6}, {0.86, 0.56}, {0.56, 0.66}}, t, true);
    } else if (n == "terminal") {
        d.rect(-0.8f, -0.62f, 0.8f, 0.62f, 0.14f, t);
        d.poly({{-0.48, -0.26}, {-0.2, 0}, {-0.48, 0.26}}, t);
        d.line(0.02f, 0.3f, 0.44f, 0.3f, t);
    } else if (n == "gear") {
        for (int i = 0; i < 8; ++i) {
            const float a = kPi * 2.0f * i / 8;
            d.line(std::cos(a) * 0.5f, std::sin(a) * 0.5f, std::cos(a) * 0.8f, std::sin(a) * 0.8f, t * 1.9f);
        }
        d.circle(0, 0, 0.52f, t * 1.2f);
        d.circle(0, 0, 0.2f, t);
    } else if (n == "sidebar") {
        d.rect(-0.8f, -0.64f, 0.8f, 0.64f, 0.14f, t);
        d.line(-0.25f, -0.64f, -0.25f, 0.64f, t);
    } else if (n == "maximize") {
        d.poly({{-0.7, -0.2}, {-0.7, -0.7}, {-0.2, -0.7}}, t);
        d.poly({{0.2, -0.7}, {0.7, -0.7}, {0.7, -0.2}}, t);
        d.poly({{0.7, 0.2}, {0.7, 0.7}, {0.2, 0.7}}, t);
        d.poly({{-0.2, 0.7}, {-0.7, 0.7}, {-0.7, 0.2}}, t);
    } else if (n == "sparkle") {
        d.polyfill({{0, -0.8}, {0.18, -0.18}, {0.8, 0}, {0.18, 0.18}, {0, 0.8}, {-0.18, 0.18}, {-0.8, 0}, {-0.18, -0.18}});
    } else if (n == "user") {
        d.circle(0, -0.3f, 0.32f, t);
        d.arc(0, 0.78f, 0.66f, kPi * 1.1f, kPi * 1.9f, t);
    } else if (n == "bell") {
        d.arc(0, -0.2f, 0.42f, kPi, kPi * 2.0f, t);
        d.poly({{-0.42, -0.2}, {-0.42, 0.28}, {-0.66, 0.5}, {0.66, 0.5}, {0.42, 0.28}, {0.42, -0.2}}, t);
        d.line(-0.14f, 0.72f, 0.14f, 0.72f, t);
        d.line(0, -0.62f, 0, -0.76f, t);
    } else if (n == "code") {
        d.poly({{-0.3, -0.5}, {-0.75, 0}, {-0.3, 0.5}}, t);
        d.poly({{0.3, -0.5}, {0.75, 0}, {0.3, 0.5}}, t);
        d.line(0.12f, -0.62f, -0.12f, 0.62f, t);
    } else if (n == "clock") {
        d.circle(0, 0, 0.74f, t);
        d.poly({{0, -0.42}, {0, 0}, {0.3, 0.2}}, t);
    } else if (n == "file") {
        d.poly({{-0.55, -0.75}, {0.18, -0.75}, {0.55, -0.38}, {0.55, 0.75}, {-0.55, 0.75}}, t, true);
        d.poly({{0.18, -0.75}, {0.18, -0.38}, {0.55, -0.38}}, t);
    } else if (n == "mic") {
        d.rect(-0.24f, -0.78f, 0.24f, 0.2f, 0.24f, t);
        d.arc(0, -0.05f, 0.46f, 0.1f, kPi - 0.1f, t);
        d.line(0, 0.42f, 0, 0.75f, t);
    } else if (n == "monitor") {
        d.rect(-0.8f, -0.62f, 0.8f, 0.34f, 0.1f, t);
        d.line(-0.35f, 0.7f, 0.35f, 0.7f, t);
        d.line(0, 0.34f, 0, 0.7f, t);
    } else if (n == "timeline") {
        d.line(-0.8f, -0.5f, 0.2f, -0.5f, t * 1.6f);
        d.line(-0.5f, 0, 0.8f, 0, t * 1.6f);
        d.line(-0.8f, 0.5f, 0.5f, 0.5f, t * 1.6f);
    } else if (n == "download") {
        d.line(0, -0.75f, 0, 0.25f, t);
        d.poly({{-0.38, -0.1}, {0, 0.28}, {0.38, -0.1}}, t);
        d.poly({{-0.72, 0.3}, {-0.72, 0.72}, {0.72, 0.72}, {0.72, 0.3}}, t);
    } else if (n == "link") {
        d.rect(-0.82f, -0.3f, 0.08f, 0.3f, 0.3f, t);
        d.rect(-0.08f, -0.3f, 0.82f, 0.3f, 0.3f, t);
    } else if (n == "render") {   // кадр плівки зі стрілкою відтворення
        d.rect(-0.8f, -0.6f, 0.8f, 0.6f, 0.14f, t);
        d.polyfill({{-0.22, -0.32}, {0.34, 0}, {-0.22, 0.32}});
    } else if (n == "wave") {    // звукова хвиля (робочий простір «Звук»)
        const float bars[] = {0.25f, 0.55f, 0.85f, 0.45f, 0.7f, 0.3f};
        for (int i = 0; i < 6; ++i) {
            const float x = -0.7f + i * 0.28f;
            d.line(x, -bars[i] * 0.8f, x, bars[i] * 0.8f, t * 1.3f);
        }
    } else if (n == "translate") {
        d.rect(-0.8f, -0.7f, 0.2f, 0.25f, 0.12f, t);
        d.rect(-0.2f, -0.25f, 0.8f, 0.7f, 0.12f, t);
        d.line(-0.55f, -0.3f, -0.3f, -0.3f, t);
        d.line(0.1f, 0.25f, 0.5f, 0.25f, t);
    } else if (n == "edit") {    // монтаж: доріжки з кліпами
        d.rectfill(-0.8f, -0.6f, 0.1f, -0.25f, 0.08f);
        d.rectfill(-0.4f, -0.1f, 0.8f, 0.25f, 0.08f);
        d.rectfill(-0.8f, 0.4f, -0.1f, 0.75f, 0.08f);
        d.line(0.35f, -0.85f, 0.35f, 0.85f, t);
    } else if (n == "project") {  // папка з демо
        d.rect(-0.75f, -0.65f, 0.75f, 0.65f, 0.14f, t);
        d.line(-0.75f, -0.3f, 0.75f, -0.3f, t);
        d.dot(-0.5f, -0.48f, t * 0.8f);
        d.dot(-0.3f, -0.48f, t * 0.8f);
        d.polyfill({{-0.15, -0.05}, {0.3, 0.2}, {-0.15, 0.45}});
    } else if (n == "cpu") {
        d.rect(-0.5f, -0.5f, 0.5f, 0.5f, 0.1f, t);
        d.rect(-0.2f, -0.2f, 0.2f, 0.2f, 0.04f, t);
        for (float x : {-0.25f, 0.0f, 0.25f}) {
            d.line(x, -0.5f, x, -0.75f, t);
            d.line(x, 0.5f, x, 0.75f, t);
            d.line(-0.5f, x, -0.75f, x, t);
            d.line(0.5f, x, 0.75f, x, t);
        }
    } else if (n == "external") {
        d.poly({{0.1, -0.7}, {0.7, -0.7}, {0.7, -0.1}}, t);
        d.line(0.7f, -0.7f, -0.05f, 0.05f, t);
        d.poly({{-0.2, -0.55}, {-0.7, -0.55}, {-0.7, 0.7}, {0.55, 0.7}, {0.55, 0.2}}, t);
    } else if (n == "copy") {
        d.rect(-0.7f, -0.45f, 0.25f, 0.75f, 0.1f, t);
        d.poly({{-0.4, -0.7}, {0.7, -0.7}, {0.7, 0.45}}, t);
    } else if (n == "trash") {
        d.line(-0.7f, -0.5f, 0.7f, -0.5f, t);
        d.poly({{-0.25, -0.5}, {-0.25, -0.72}, {0.25, -0.72}, {0.25, -0.5}}, t);
        d.poly({{-0.55, -0.5}, {-0.45, 0.75}, {0.45, 0.75}, {0.55, -0.5}}, t);
    } else if (n == "dots") {
        for (float x : {-0.55f, 0.0f, 0.55f}) d.dot(x, 0, t * 1.2f);
    }
}

LogoItem::LogoItem(QQuickItem* parent) : QQuickPaintedItem(parent) { setAntialiasing(true); }

void LogoItem::setAccent(const QColor& c) {
    if (c == accent_) return;
    accent_ = c;
    update();
    emit accentChanged();
}

// Заокруглений квадрат із діагональним градієнтом акценту і кадром плівки зі стрілкою
void LogoItem::paint(QPainter* p) {
    p->setRenderHint(QPainter::Antialiasing);
    const qreal s = std::min(width(), height());
    QLinearGradient g(0, 0, s, s);
    auto mix = [](QColor a, QColor b, qreal k) {
        return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * k, a.greenF() + (b.greenF() - a.greenF()) * k,
                                a.blueF() + (b.blueF() - a.blueF()) * k);
    };
    g.setColorAt(0, mix(accent_, Qt::white, 0.12));
    g.setColorAt(1, mix(accent_, QColor(24, 180, 230), 0.55));
    p->setPen(Qt::NoPen);
    p->setBrush(g);
    p->drawRoundedRect(QRectF(0, 0, s, s), s * 0.26, s * 0.26);
    const QPointF c(s / 2, s / 2);
    const qreal fw = s * 0.6, fh = s * 0.46, t = std::max(1.0, s * 0.07);
    p->setPen(QPen(QColor(255, 255, 255, 235), t));
    p->setBrush(Qt::NoBrush);
    p->drawRoundedRect(QRectF(c.x() - fw / 2, c.y() - fh / 2, fw, fh), s * 0.08, s * 0.08);
    const qreal tr = fh * 0.3;
    p->setPen(Qt::NoPen);
    p->setBrush(Qt::white);
    p->drawPolygon(QPolygonF({QPointF(c.x() - tr * 0.7, c.y() - tr), QPointF(c.x() + tr * 1.05, c.y()),
                              QPointF(c.x() - tr * 0.7, c.y() + tr)}));
    p->setBrush(QColor(255, 255, 255, 200));
    for (qreal dx : {-0.18, 0.18}) {
        p->drawEllipse(QPointF(c.x() + dx * s, c.y() - fh / 2 - s * 0.09), s * 0.035, s * 0.035);
        p->drawEllipse(QPointF(c.x() + dx * s, c.y() + fh / 2 + s * 0.09), s * 0.035, s * 0.035);
    }
}

} // namespace gmdr::qt
