// SPDX-License-Identifier: GPL-3.0-only
/*
 *  BerryTrip Launcher - single server build of Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QVector>
#include <QWidget>

class QMouseEvent;
class QShowEvent;
class QHideEvent;
class QTimer;

namespace berry {

/// Preview of a Minecraft skin, ported from Quick Skin's in-game preview:
/// the vanilla 64x64 player model with its 0.25 px outer layers and 0.5 px
/// hat, vanilla-style face lighting, the CALM idle stance and drag-to-turn.
class Skin3D : public QWidget {
    Q_OBJECT

   public:
    explicit Skin3D(QWidget* parent = nullptr);

    void setSkin(const QImage& skin);
    void setCape(const QImage& cape);
    void setSlim(bool slim);
    void setYaw(qreal degrees);
    void resetView();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

   protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

   private:
    struct Face {
        QPolygonF screen;
        QRectF uv;
        QImage texture;
        qreal depth = 0.0;
    };

    void rebuildShades();
    void collectModel(QVector<Face>& faces) const;
    void collectPart(QVector<Face>& faces, const QVector<QImage>& textures, qreal w, qreal h, qreal d,
                     qreal cx, qreal cy, qreal cz, qreal px, qreal py, qreal pz, qreal rx, qreal ry,
                     qreal rz, QPoint uvBase, qreal inflate) const;
    void drawModel(QPainter& painter);

    QImage m_skin;
    QImage m_cape;
    QVector<QImage> m_skinShades;
    QVector<QImage> m_capeShades;
    bool m_slim = false;
    qreal m_yaw = 20.0;
    qreal m_targetYaw = 20.0;
    QPointF m_drag;
    bool m_dragging = false;
    QElapsedTimer m_clock;
    QTimer* m_timer = nullptr;
};

}  // namespace berry
