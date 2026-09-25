// =============================================================================
//  timeline_item.hpp — шкала часу демо (Timeline у QML).
//
//  Малює лінійку з таймкодами, доріжку кожного гравця (відрізки мовлення), доріжку
//  чату, фрагмент (вхід/вихід), позначки і курсор. Керування:
//    * клік або протягування по лінійці — курсор;
//    * протягування по доріжках — вибір відрізка (стане фрагментом), клік — курсор;
//    * край фрагмента чи позначку можна перетягнути;
//    * коліщатко — масштаб навколо курсора миші, Shift+коліщатко — прокрутка в часі;
//    * правий клік — контекстне меню (сигнал contextRequested).
//  Заголовки доріжок (імена, M/S, прослуховування) — у QML, з тією самою висотою
//  рядка і зсувом laneOffset.
// =============================================================================
#pragma once

#include <QColor>
#include <QQuickPaintedItem>

#include <vector>

namespace gmdr::qt {

class ProjectService;

class TimelineItem : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(double viewStart READ viewStart WRITE setViewStart NOTIFY viewChanged)
    Q_PROPERTY(double viewEnd READ viewEnd WRITE setViewEnd NOTIFY viewChanged)
    Q_PROPERTY(double duration READ duration NOTIFY viewChanged)
    Q_PROPERTY(qreal laneHeight READ laneHeight WRITE setLaneHeight NOTIFY styleChanged)
    Q_PROPERTY(qreal rulerHeight READ rulerHeight WRITE setRulerHeight NOTIFY styleChanged)
    Q_PROPERTY(qreal laneOffset READ laneOffset WRITE setLaneOffset NOTIFY laneOffsetChanged)
    Q_PROPERTY(int laneCount READ laneCount NOTIFY lanesChanged)
    Q_PROPERTY(qreal fontPixelSize READ fontPixelSize WRITE setFontPixelSize NOTIFY styleChanged)
    Q_PROPERTY(QColor backgroundColor MEMBER bg_ NOTIFY styleChanged)
    Q_PROPERTY(QColor laneColor MEMBER lane_ NOTIFY styleChanged)
    Q_PROPERTY(QColor gridColor MEMBER grid_ NOTIFY styleChanged)
    Q_PROPERTY(QColor textColor MEMBER text_ NOTIFY styleChanged)
    Q_PROPERTY(QColor dimTextColor MEMBER dim_ NOTIFY styleChanged)
    Q_PROPERTY(QColor accentColor MEMBER accent_ NOTIFY styleChanged)
    Q_PROPERTY(QColor markerColor MEMBER marker_ NOTIFY styleChanged)
    Q_PROPERTY(QColor mutedColor MEMBER muted_ NOTIFY styleChanged)
    Q_PROPERTY(QVariantList laneColors MEMBER lane_colors_ NOTIFY styleChanged)
    Q_PROPERTY(bool interactive MEMBER interactive_ NOTIFY styleChanged)

public:
    explicit TimelineItem(QQuickItem* parent = nullptr);
    static void set_project(ProjectService* p) { s_project = p; }

    void paint(QPainter* p) override;

    double viewStart() const { return v0_; }
    double viewEnd() const { return v1_; }
    void   setViewStart(double t);
    void   setViewEnd(double t);
    double duration() const;
    qreal  laneHeight() const { return lane_h_; }
    void   setLaneHeight(qreal h);
    qreal  rulerHeight() const { return ruler_h_; }
    void   setRulerHeight(qreal h);
    qreal  laneOffset() const { return lane_offset_; }
    void   setLaneOffset(qreal y);
    int    laneCount() const;
    qreal  fontPixelSize() const { return font_px_; }
    void   setFontPixelSize(qreal px);

    Q_INVOKABLE void   zoomAt(double t, double factor);
    Q_INVOKABLE void   showAll();
    Q_INVOKABLE void   showRange(double from, double to);
    Q_INVOKABLE void   ensureVisible(double t);
    Q_INVOKABLE double timeAt(qreal x) const;
    Q_INVOKABLE qreal  xAt(double t) const;

signals:
    void viewChanged();
    void styleChanged();
    void lanesChanged();
    void laneOffsetChanged();
    void rangeSelected(double from, double to);
    void contextRequested(double time, qreal x, qreal y, int marker);
    void markerActivated(int index);

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void hoverMoveEvent(QHoverEvent* e) override;

private:
    enum class Drag { None, Seek, Select, In, Out, Marker };
    int    marker_near(qreal x) const;
    Drag   edge_near(qreal x) const;
    void   clamp_view();
    qreal  chat_row_h() const { return lane_h_ * 0.6; }

    static ProjectService* s_project;
    double       v0_ = 0, v1_ = 0;
    qreal        lane_h_ = 28, ruler_h_ = 26, lane_offset_ = 0, font_px_ = 11;
    QColor       bg_{0x18, 0x1A, 0x1D}, lane_{0x1E, 0x20, 0x23}, grid_{0x30, 0x34, 0x3A}, text_{0xF1, 0xF3, 0xF5},
                 dim_{0x74, 0x7B, 0x84}, accent_{0x4C, 0xC2, 0xFF}, marker_{0xF0, 0xB4, 0x29}, muted_{0x58, 0x58, 0x58};
    QVariantList lane_colors_;
    bool         interactive_ = true;
    Drag         drag_ = Drag::None;
    int          drag_marker_ = -1;
    double       drag_from_ = 0, drag_to_ = 0;
    qreal        press_x_ = 0;
};

} // namespace gmdr::qt
