// SPDX-License-Identifier: GPL-3.0-only
/*
 *  BerryTrip Launcher - single server build of Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QImage>
#include <QPoint>
#include <QPolygonF>
#include <QRectF>
#include <QVector>
#include <QVector3D>
#include <QWidget>

class QShowEvent;
class QHideEvent;

class QTimer;

namespace berry {

/// Software-rendered 3D preview of a Minecraft skin: the same orthographic
/// model Quick Skin draws, with auto-rotation and drag-to-rotate.
class Skin3D : public QWidget {
    Q_OBJECT

   public:
    explicit Skin3D(QWidget* parent = nullptr);

    void setSkin(const QImage& skin);
    void setCape(const QImage& cape);
    void setSlim(bool slim);
    void resetView();

    QSize sizeHint() const override;

   protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

   private:
    struct Face {
        QPolygonF screen;
        QRectF uv;
        QImage texture;
        qreal depth = 0;
        bool overlay = false;
    };

    void drawModel(QPainter& painter);
    void collectPart(QVector<Face>& faces, const QImage& texture, qreal sizeW, qreal sizeH, qreal sizeD,
                     qreal cx, qreal cy, qreal cz, QPoint uvBase, qreal inflate, bool capePart) const;

    QImage m_skin;
    QImage m_cape;
    bool m_slim = false;
    qreal m_yaw = 22.0;
    qreal m_pitch = 0.0;
    qreal m_sway = 0.0;
    QPoint m_drag;
    bool m_dragging = false;
    qint64 m_lastDrag = 0;
    QTimer* m_timer = nullptr;
};

}  // namespace berry
