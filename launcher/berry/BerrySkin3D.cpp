// SPDX-License-Identifier: GPL-3.0-only
/*
 *  BerryTrip Launcher - single server build of Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "berry/BerrySkin3D.h"

#include <QDateTime>
#include <QMouseEvent>
#include <QPainter>
#include <QTransform>
#include <QVector>
#include <QPolygonF>
#include <QTimer>
#include <QVector3D>
#include <QtMath>

#include <algorithm>

namespace berry {

namespace {
constexpr qreal TickMs = 40.0;
constexpr qreal DegreesPerTick = 0.35;

struct P3 {
    qreal x, y, z;
};

P3 rotatePoint(const P3& p, qreal yaw, qreal pitch)
{
    const qreal cy = qCos(yaw), sy = qSin(yaw);
    const qreal cp = qCos(pitch), sp = qSin(pitch);
    const qreal x1 = p.x * cy - p.z * sy;
    const qreal z1 = p.x * sy + p.z * cy;
    const qreal y1 = p.y * cp - z1 * sp;
    const qreal z2 = p.y * sp + z1 * cp;
    return { x1, y1, z2 };
}
}  // namespace

Skin3D::Skin3D(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(false);
    setCursor(Qt::OpenHandCursor);
    setToolTip(tr("Потяни мышью — модель повернётся"));

    m_timer = new QTimer(this);
    m_timer->setInterval(static_cast<int>(TickMs));
    connect(m_timer, &QTimer::timeout, this, [this] {
        if (!isVisible() || m_dragging)
            return;
        if (QDateTime::currentMSecsSinceEpoch() - m_lastDrag < 1600)
            return;
        m_yaw += DegreesPerTick;
        m_sway += 0.08;
        update();
    });
}

QSize Skin3D::sizeHint() const
{
    return { 320, 400 };
}

void Skin3D::setSkin(const QImage& skin)
{
    if (!skin.isNull() && skin.height() < 64) {
        QImage fixed(skin.width(), 64, QImage::Format_ARGB32_Premultiplied);
        fixed.fill(Qt::transparent);
        QPainter p(&fixed);
        p.drawImage(0, 0, skin);
        m_skin = fixed;
    } else {
        m_skin = skin;
    }
    update();
}

void Skin3D::setCape(const QImage& cape)
{
    m_cape = cape;
    update();
}

void Skin3D::setSlim(bool slim)
{
    m_slim = slim;
    update();
}

void Skin3D::resetView()
{
    m_yaw = 22.0;
    m_pitch = 0.0;
    m_sway = 0.0;
    update();
}

void Skin3D::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    m_timer->start();
}

void Skin3D::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    m_timer->stop();
}

void Skin3D::mousePressEvent(QMouseEvent* event)
{
    m_dragging = true;
    m_drag = event->pos();
    m_lastDrag = QDateTime::currentMSecsSinceEpoch();
    setCursor(Qt::ClosedHandCursor);
}

void Skin3D::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_dragging)
        return;

    const QPointF pos = event->position();
    m_yaw += (pos.x() - m_drag.x()) * 0.7;
    m_pitch = qBound(-24.0, m_pitch - (pos.y() - m_drag.y()) * 0.35, 24.0);
    m_drag = pos.toPoint();
    m_lastDrag = QDateTime::currentMSecsSinceEpoch();
    update();
}

void Skin3D::mouseReleaseEvent(QMouseEvent* event)
{
    Q_UNUSED(event);
    m_dragging = false;
    m_lastDrag = QDateTime::currentMSecsSinceEpoch();
    setCursor(Qt::OpenHandCursor);
}

void Skin3D::collectPart(QVector<Face>& faces, const QImage& texture, qreal sizeW, qreal sizeH, qreal sizeD,
                         qreal cx, qreal cy, qreal cz, QPoint uvBase, qreal inflate, bool capePart) const
{
    if (texture.isNull())
        return;

    const qreal hx = sizeW / 2.0 + inflate;
    const qreal hy = sizeH / 2.0 + inflate;
    const qreal hz = sizeD / 2.0 + inflate;
    const qreal texel = texture.width() / 64.0;

    const int w = int(sizeW), h = int(sizeH), d = int(sizeD);
    const int u = uvBase.x(), v = uvBase.y();

    struct Def {
        P3 verts[4];
        P3 normal;
        int tu, tv, tw, th;
    };

    const Def defs[] = {
        // front (+z)
        { { { -hx, hy, hz }, { hx, hy, hz }, { hx, -hy, hz }, { -hx, -hy, hz } }, { 0, 0, 1 }, u + d, v + d, w, h },
        // back (-z)
        { { { hx, hy, -hz }, { -hx, hy, -hz }, { -hx, -hy, -hz }, { hx, -hy, -hz } }, { 0, 0, -1 }, u + d + w, v + d,
          w, h },
        // right (+x)
        { { { hx, hy, hz }, { hx, hy, -hz }, { hx, -hy, -hz }, { hx, -hy, hz } }, { 1, 0, 0 }, u, v + d, d, h },
        // left (-x)
        { { { -hx, hy, -hz }, { -hx, hy, hz }, { -hx, -hy, hz }, { -hx, -hy, -hz } }, { -1, 0, 0 },
          u + d + w, v + d, d, h },
        // top (+y)
        { { { -hx, hy, -hz }, { hx, hy, -hz }, { hx, hy, hz }, { -hx, hy, hz } }, { 0, 1, 0 }, u + d, v, w, d },
        // bottom (-y)
        { { { -hx, -hy, hz }, { hx, -hy, hz }, { hx, -hy, -hz }, { -hx, -hy, -hz } }, { 0, -1, 0 }, u + d + w, v,
          w, d },
    };

    for (const Def& def : defs) {
        const P3 normal = rotatePoint(def.normal, qDegreesToRadians(m_yaw), qDegreesToRadians(m_pitch));
        if (normal.z <= 0.0)
            continue;

        Face face;
        face.texture = texture;
        face.uv = QRectF(def.tu * texel, def.tv * texel, def.tw * texel, def.th * texel);
        face.overlay = inflate > 0.0;

        qreal depth = 0;
        for (int i = 0; i < 4; ++i) {
            P3 p{ def.verts[i].x + cx, def.verts[i].y + cy, def.verts[i].z + cz };
            if (capePart) {
                const qreal hinge = cy + sizeH / 2.0;
                p.z += (p.y - hinge) * qSin(m_sway) * 0.25;
            }
            const P3 r = rotatePoint(p, qDegreesToRadians(m_yaw), qDegreesToRadians(m_pitch));
            face.screen << QPointF(r.x, -r.y);
            depth += r.z;
        }
        face.depth = depth / 4.0;
        faces.append(face);
    }
}

void Skin3D::drawModel(QPainter& painter)
{
    const qreal scale = qMin(width() / 44.0, height() / 46.0);
    if (scale <= 0.0)
        return;

    QVector<Face> faces;

    const bool slim = m_slim;
    const qreal arm = slim ? 3.0 : 4.0;
    const qreal armX = slim ? 5.5 : 6.0;

    // Model space: y up, feet at -12, head top at 32, z towards the viewer.
    collectPart(faces, m_skin, 4, 12, 4, -2, -6, 0, { 0, 16 }, 0, false);
    collectPart(faces, m_skin, 4, 12, 4, 2, -6, 0, { 16, 48 }, 0, false);
    collectPart(faces, m_skin, 4, 12, 4, -2, -6, 0, { 0, 32 }, 0.5, false);
    collectPart(faces, m_skin, 4, 12, 4, 2, -6, 0, { 0, 48 }, 0.5, false);
    collectPart(faces, m_skin, 8, 12, 4, 0, 6, 0, { 16, 16 }, 0, false);
    collectPart(faces, m_skin, 8, 12, 4, 0, 6, 0, { 16, 32 }, 0.5, false);
    collectPart(faces, m_skin, arm, 12, 4, -4 - armX, 6, 0, { 40, 16 }, 0, false);
    collectPart(faces, m_skin, arm, 12, 4, 4 + armX, 6, 0, { 32, 48 }, 0, false);
    collectPart(faces, m_skin, arm, 12, 4, -4 - armX, 6, 0, { 40, 32 }, 0.5, false);
    collectPart(faces, m_skin, arm, 12, 4, 4 + armX, 6, 0, { 48, 48 }, 0.5, false);
    collectPart(faces, m_skin, 8, 8, 8, 0, 20, 0, { 0, 0 }, 0, false);
    collectPart(faces, m_skin, 8, 8, 8, 0, 20, 0, { 32, 0 }, 0.5, false);

    if (!m_cape.isNull())
        collectPart(faces, m_cape, 10, 16, 1, 0, 4, -3.2, { 0, 0 }, 0, true);

    std::sort(faces.begin(), faces.end(), [](const Face& a, const Face& b) { return a.depth < b.depth; });

    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.setPen(Qt::NoPen);

    const QPointF origin(width() / 2.0, height() / 2.0 + scale * 4.0);
    for (auto& face : faces) {
        QPolygonF target;
        for (const QPointF& point : face.screen)
            target << origin + point * scale;

        QPolygonF source;
        source << QPointF(0, 0) << QPointF(1, 0) << QPointF(1, 1) << QPointF(0, 1);

        QTransform transform;
        if (!QTransform::quadToQuad(source, target, transform))
            continue;

        painter.setTransform(transform);
        painter.setOpacity(face.overlay ? 0.95 : 1.0);
        painter.drawImage(QRectF(0, 0, 1, 1), face.texture, face.uv);
        painter.resetTransform();
    }
    painter.setOpacity(1.0);
}

void Skin3D::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);

    if (m_skin.isNull()) {
        painter.setPen(QColor(0x7C, 0x87, 0x98));
        painter.drawText(rect(), Qt::AlignCenter, tr("НЕТ СКИНА"));
        return;
    }

    drawModel(painter);
}

}  // namespace berry
