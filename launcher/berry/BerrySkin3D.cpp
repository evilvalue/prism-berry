// SPDX-License-Identifier: GPL-3.0-only
/*
 *  BerryTrip Launcher - single server build of Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "berry/BerrySkin3D.h"

#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QTransform>
#include <QtMath>

#include <algorithm>

namespace berry {

namespace {

constexpr int ShadeSteps = 12;
constexpr qreal ShadeMin = 0.35;
constexpr qreal ShadeMax = 1.0;

struct V3 {
    qreal x, y, z;
};

V3 operator+(const V3& a, const V3& b)
{
    return { a.x + b.x, a.y + b.y, a.z + b.z };
}

V3 operator-(const V3& a, const V3& b)
{
    return { a.x - b.x, a.y - b.y, a.z - b.z };
}

qreal dot(const V3& a, const V3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

V3 cross(const V3& a, const V3& b)
{
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}

V3 normalized(const V3& v)
{
    const qreal length = qSqrt(dot(v, v));
    return length > 0.0 ? V3{ v.x / length, v.y / length, v.z / length } : v;
}

V3 rotatePart(const V3& p, qreal rx, qreal ry, qreal rz)
{
    // ModelPart order: Z, then Y, then X.
    const qreal cx = qCos(rx), sx = qSin(rx);
    const qreal cy = qCos(ry), sy = qSin(ry);
    const qreal cz = qCos(rz), sz = qSin(rz);

    const qreal x1 = p.x * cy + p.z * sy;
    const qreal z1 = -p.x * sy + p.z * cy;
    const qreal y2 = p.y * cx - z1 * sx;
    const qreal z2 = p.y * sx + z1 * cx;
    return { x1 * cz - y2 * sz, x1 * sz + y2 * cz, z2 };
}

V3 rotateYaw(const V3& p, qreal degrees)
{
    const qreal a = qDegreesToRadians(degrees);
    const qreal c = qCos(a), s = qSin(a);
    return { p.x * c + p.z * s, p.y, -p.x * s + p.z * c };
}

QImage tinted(const QImage& source, qreal factor)
{
    QImage image = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < image.height(); ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const QRgb pixel = line[x];
            line[x] = qRgba(int(qRed(pixel) * factor), int(qGreen(pixel) * factor), int(qBlue(pixel) * factor),
                            qAlpha(pixel));
        }
    }
    return image;
}

int shadeBucket(qreal shade)
{
    const qreal position = (qBound(ShadeMin, shade, ShadeMax) - ShadeMin) / (ShadeMax - ShadeMin);
    return qBound(0, qRound(position * (ShadeSteps - 1)), ShadeSteps - 1);
}

}  // namespace

Skin3D::Skin3D(QWidget* parent) : QWidget(parent)
{
    setCursor(Qt::OpenHandCursor);
    setToolTip(tr("Потяни мышью — модель повернётся"));

    m_clock.start();

    m_timer = new QTimer(this);
    m_timer->setInterval(33);
    connect(m_timer, &QTimer::timeout, this, [this] {
        if (!isVisible())
            return;

        if (!m_dragging) {
            qreal delta = m_targetYaw - m_yaw;
            while (delta > 180.0)
                delta -= 360.0;
            while (delta < -180.0)
                delta += 360.0;
            if (qAbs(delta) > 0.02)
                m_yaw += delta * 0.18;
            else
                m_yaw = m_targetYaw;
        }
        update();
    });
}

QSize Skin3D::sizeHint() const
{
    return { 300, 380 };
}

QSize Skin3D::minimumSizeHint() const
{
    return { 220, 280 };
}

void Skin3D::setSkin(const QImage& skin)
{
    m_skin = skin.isNull() ? QImage() : skin.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    rebuildShades();
    update();
}

void Skin3D::setCape(const QImage& cape)
{
    m_cape = cape.isNull() ? QImage() : cape.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    rebuildShades();
    update();
}

void Skin3D::setSlim(bool slim)
{
    if (m_slim == slim)
        return;
    m_slim = slim;
    update();
}

void Skin3D::setYaw(qreal degrees)
{
    m_yaw = degrees;
    m_targetYaw = degrees;
    update();
}

void Skin3D::resetView()
{
    m_targetYaw = 20.0;
    update();
}

void Skin3D::rebuildShades()
{
    m_skinShades.clear();
    m_capeShades.clear();
    if (m_skin.isNull())
        return;

    for (int i = 0; i < ShadeSteps; ++i) {
        const qreal factor = ShadeMin + (ShadeMax - ShadeMin) * i / (ShadeSteps - 1);
        m_skinShades.append(tinted(m_skin, factor));
        if (!m_cape.isNull())
            m_capeShades.append(tinted(m_cape, factor));
    }
}

void Skin3D::collectPart(QVector<Face>& faces, const QVector<QImage>& textures, qreal sizeW, qreal sizeH,
                         qreal sizeD, qreal cx, qreal cy, qreal cz, qreal px, qreal py, qreal pz, qreal rx,
                         qreal ry, qreal rz, QPoint uvBase, qreal inflate) const
{
    if (textures.isEmpty())
        return;

    const qreal hx = sizeW / 2.0 + inflate;
    const qreal hy = sizeH / 2.0 + inflate;
    const qreal hz = sizeD / 2.0 + inflate;
    const qreal texel = textures.first().width() / 64.0;

    const int w = qRound(sizeW), h = qRound(sizeH), d = qRound(sizeD);
    const int u = uvBase.x(), v = uvBase.y();

    const V3 local[8] = {
        { cx - hx, cy + hy, cz + hz },  // 0 front top left
        { cx + hx, cy + hy, cz + hz },  // 1 front top right
        { cx + hx, cy - hy, cz + hz },  // 2 front bottom right
        { cx - hx, cy - hy, cz + hz },  // 3 front bottom left
        { cx + hx, cy + hy, cz - hz },  // 4 back top right
        { cx - hx, cy + hy, cz - hz },  // 5 back top left
        { cx - hx, cy - hy, cz - hz },  // 6 back bottom left
        { cx + hx, cy - hy, cz - hz },  // 7 back bottom right
    };

    struct FaceDef {
        int indices[4];
        int tu, tv, tw, th;
    };

    const FaceDef defs[] = {
        { { 0, 1, 2, 3 }, u + d, v + d, w, h },              // front (+z)
        { { 4, 5, 6, 7 }, u + d + w + d, v + d, w, h },      // back (-z)
        { { 1, 4, 7, 2 }, u + d + w, v + d, d, h },          // left side of the model (+x)
        { { 5, 0, 3, 6 }, u, v + d, d, h },                  // right side of the model (-x)
        { { 5, 4, 1, 0 }, u + d, v, w, d },                  // top (+y)
        { { 3, 2, 7, 6 }, u + d + w, v, w, d },              // bottom (-y)
    };

    const qreal yaw = m_yaw;
    const V3 keyLight = normalized({ -0.40, 1.0, 0.75 });
    const V3 fillLight = normalized({ 0.55, 0.55, 0.90 });

    for (const FaceDef& def : defs) {
        V3 corners[4];
        for (int i = 0; i < 4; ++i) {
            V3 p = local[def.indices[i]];
            const V3 relative{ p.x - px, p.y - py, p.z - pz };
            const V3 rotated = rotatePart(relative, rx, ry, rz);
            p = { rotated.x + px, rotated.y + py, rotated.z + pz };
            p = rotateYaw({ p.x, p.y - 16.0, p.z }, yaw);
            corners[i] = { p.x, p.y + 16.0, p.z };
        }

        const V3 normal = normalized(cross(corners[3] - corners[0], corners[1] - corners[0]));
        if (normal.z <= 0.001)
            continue;

        const qreal shade = 0.46 + 0.40 * qMax<qreal>(0.0, dot(normal, keyLight)) +
                            0.18 * qMax<qreal>(0.0, dot(normal, fillLight));
        const int bucket = shadeBucket(shade);

        Face face;
        face.texture = textures.at(bucket);
        face.uv = QRectF(def.tu * texel, def.tv * texel, def.tw * texel, def.th * texel);
        face.depth = (corners[0].z + corners[1].z + corners[2].z + corners[3].z) / 4.0 + inflate * 0.05;
        for (const V3& corner : corners)
            face.screen << QPointF(corner.x, -corner.y);
        faces.append(face);
    }
}

void Skin3D::collectModel(QVector<Face>& faces) const
{
    if (m_skinShades.isEmpty())
        return;

    const qreal t = m_clock.elapsed() / 1000.0;
    const qreal breath = qSin(2.0 * M_PI * t / 4.0);
    const qreal drift = qSin(2.0 * M_PI * t / 7.0);
    const qreal sway = qSin(2.0 * M_PI * t / 5.5 + 1.3);
    const qreal armOut = 0.13 + (breath * 0.5 + 0.5) * 0.035;

    const qreal headX = qDegreesToRadians(-0.02 + breath * 0.02);
    const qreal headY = qDegreesToRadians(drift * 0.08);
    const qreal headZ = qDegreesToRadians(sway * 0.025);
    const qreal bodyX = qDegreesToRadians(breath * 0.012);
    const qreal armSwing = qDegreesToRadians(sway * 0.035);

    const QVector<QImage>& skin = m_skinShades;

    // Body and head share the neck pivot (0, 24), the arms hang from (±5, 22)
    // and the legs from the hips (±1.9, 12), exactly as the vanilla player model.
    collectPart(faces, skin, 8, 12, 4, 0, 18, 0, 0, 24, 0, bodyX, 0, 0, { 16, 16 }, 0);
    collectPart(faces, skin, 8, 12, 4, 0, 18, 0, 0, 24, 0, bodyX, 0, 0, { 16, 32 }, 0.25);

    const qreal armWidth = m_slim ? 3.0 : 4.0;
    const qreal armCenter = m_slim ? 5.5 : 6.0;
    const QPoint rightArm = m_slim ? QPoint(44, 16) : QPoint(40, 16);
    const QPoint rightSleeve = m_slim ? QPoint(44, 32) : QPoint(40, 32);
    const QPoint leftArm = m_slim ? QPoint(36, 48) : QPoint(32, 48);
    const QPoint leftSleeve = m_slim ? QPoint(52, 48) : QPoint(48, 48);

    collectPart(faces, skin, armWidth, 12, 4, -armCenter, 18, 0, -5, 22, 0, armSwing, 0, -armOut, rightArm, 0);
    collectPart(faces, skin, armWidth, 12, 4, -armCenter, 18, 0, -5, 22, 0, armSwing, 0, -armOut, rightSleeve, 0.25);
    collectPart(faces, skin, armWidth, 12, 4, armCenter, 18, 0, 5, 22, 0, -armSwing, 0, armOut, leftArm, 0);
    collectPart(faces, skin, armWidth, 12, 4, armCenter, 18, 0, 5, 22, 0, -armSwing, 0, armOut, leftSleeve, 0.25);

    collectPart(faces, skin, 8, 8, 8, 0, 28, 0, 0, 24, 0, headX, headY, headZ, { 0, 0 }, 0);
    collectPart(faces, skin, 8, 8, 8, 0, 28, 0, 0, 24, 0, headX, headY, headZ, { 32, 0 }, 0.5);

    collectPart(faces, skin, 4, 12, 4, -1.9, 6, 0, -1.9, 12, 0, 0, 0, -0.07, { 0, 16 }, 0);
    collectPart(faces, skin, 4, 12, 4, -1.9, 6, 0, -1.9, 12, 0, 0, 0, -0.07, { 0, 32 }, 0.25);
    collectPart(faces, skin, 4, 12, 4, 1.9, 6, 0, 1.9, 12, 0, 0, 0, 0.07, { 16, 48 }, 0);
    collectPart(faces, skin, 4, 12, 4, 1.9, 6, 0, 1.9, 12, 0, 0, 0, 0.07, { 0, 48 }, 0.25);

    if (!m_capeShades.isEmpty())
        collectPart(faces, m_capeShades, 10, 16, 1, 0, 16, -2.6, 0, 24, -2.6, 0, 0, 0, { 0, 0 }, 0);
}

void Skin3D::drawModel(QPainter& painter)
{
    QVector<Face> faces;
    collectModel(faces);
    if (faces.isEmpty())
        return;

    std::sort(faces.begin(), faces.end(), [](const Face& a, const Face& b) { return a.depth < b.depth; });

    QRectF bounds;
    bool first = true;
    for (const Face& face : faces) {
        for (const QPointF& point : face.screen) {
            if (first) {
                bounds = QRectF(point, QSizeF(0.0, 0.0));
                first = false;
                continue;
            }
            bounds.setLeft(qMin(bounds.left(), point.x()));
            bounds.setTop(qMin(bounds.top(), point.y()));
            bounds.setRight(qMax(bounds.right(), point.x()));
            bounds.setBottom(qMax(bounds.bottom(), point.y()));
        }
    }
    if (bounds.width() <= 0.0 || bounds.height() <= 0.0)
        return;

    const qreal padding = 14.0;
    const qreal scale = qMin((width() - padding * 2.0) / bounds.width(), (height() - padding * 2.0) / bounds.height());
    const QPointF origin(width() / 2.0 - bounds.center().x() * scale,
                         height() / 2.0 - bounds.center().y() * scale);

    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(Qt::NoPen);

    QPolygonF source;
    source << QPointF(0, 0) << QPointF(1, 0) << QPointF(1, 1) << QPointF(0, 1);

    for (const Face& face : faces) {
        QPolygonF target;
        for (const QPointF& point : face.screen)
            target << origin + point * scale;

        QTransform transform;
        if (!QTransform::quadToQuad(source, target, transform))
            continue;

        painter.setTransform(transform);
        painter.drawImage(QRectF(0, 0, 1, 1), face.texture, face.uv);
        painter.resetTransform();
    }
}

void Skin3D::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.fillRect(rect(), Qt::transparent);

    if (m_skin.isNull()) {
        painter.setPen(QColor(0x7C, 0x87, 0x98));
        painter.drawText(rect(), Qt::AlignCenter, tr("НЕТ СКИНА"));
        return;
    }

    // Мягкое ступенчатое свечение-сцена за моделью: силуэт не сливается с тёмной карточкой.
    {
        const QColor base(0x0F, 0x13, 0x1B);
        const QColor glow(0x3A, 0x24, 0x48);
        const QPointF center(width() / 2.0, height() * 0.46);
        const qreal rx = qMin(width() * 0.45, height() * 0.31);
        const qreal ry = rx * 1.18;
        constexpr int Rings = 9;
        for (int i = 0; i < Rings; ++i) {
            const qreal t = qreal(i + 1) / Rings;
            const qreal scale = 1.0 - 0.07 * i;
            const QColor tint(int(base.red() + (glow.red() - base.red()) * t * 0.8),
                              int(base.green() + (glow.green() - base.green()) * t * 0.8),
                              int(base.blue() + (glow.blue() - base.blue()) * t * 0.8));
            const qreal ryRing = ry * scale;
            for (qreal y = -ryRing; y < ryRing; y += 2.0) {
                const qreal k = 1.0 - (y * y) / (ryRing * ryRing);
                if (k <= 0)
                    continue;
                const qreal half = rx * scale * qSqrt(k);
                painter.fillRect(QRectF(center.x() - half, center.y() + y, half * 2.0, 2.0), tint);
            }
        }
    }

    drawModel(painter);
}

void Skin3D::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    m_clock.restart();
    m_timer->start();
}

void Skin3D::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    m_timer->stop();
}

void Skin3D::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    m_dragging = true;
    m_drag = event->position();
    setCursor(Qt::ClosedHandCursor);
}

void Skin3D::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_dragging)
        return;
    const QPointF position = event->position();
    m_targetYaw += (position.x() - m_drag.x()) * 0.55;
    m_drag = position;
    update();
}

void Skin3D::mouseReleaseEvent(QMouseEvent* event)
{
    Q_UNUSED(event);
    m_dragging = false;
    setCursor(Qt::OpenHandCursor);
}

void Skin3D::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        resetView();
}

}  // namespace berry
