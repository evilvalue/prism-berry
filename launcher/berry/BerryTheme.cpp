// SPDX-License-Identifier: GPL-3.0-only
/*
 *  BerryTrip Launcher - single server build of Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "berry/BerryTheme.h"

#include <QApplication>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QStyleFactory>
#include <QStringList>
#include <QWidget>

namespace berry::theme {

namespace {
QString g_family;
QString g_familySemi;

QString registerFont(const QString& path)
{
    const int id = QFontDatabase::addApplicationFont(path);
    if (id < 0)
        return {};
    const QStringList families = QFontDatabase::applicationFontFamilies(id);
    return families.isEmpty() ? QString() : families.first();
}

void plot(QPainter& painter, int x, int y, int scale, const QColor& color)
{
    painter.fillRect(QRect(x * scale, y * scale, scale, scale), color);
}
}  // namespace

void init()
{
    if (!g_family.isEmpty())
        return;

    g_family = registerFont(QStringLiteral(":/berry/MinecraftRus.ttf"));
    g_familySemi = g_family;
    if (g_family.isEmpty())
        g_family = QStringLiteral("Consolas");
    if (g_familySemi.isEmpty())
        g_familySemi = g_family;
}

QString family()
{
    init();
    return g_family;
}

QFont regular(int pixelSize)
{
    init();
    // Minecraft Rus metrics are huge (1em ~ 4x pixel grid). Clamp so callers
    // can pass design sizes without blowing out every layout.
    const int px = qBound(8, pixelSize, 16);
    QFont font(g_family);
    font.setPixelSize(px);
    font.setStyleStrategy(QFont::NoAntialias);
    return font;
}

QFont semiBold(int pixelSize)
{
    init();
    const int px = qBound(8, pixelSize, 16);
    QFont font(g_familySemi);
    font.setPixelSize(px);
    font.setBold(true);
    font.setStyleStrategy(QFont::NoAntialias);
    return font;
}

QFont spaced(int pixelSize, qreal spacing, bool bold)
{
    init();
    const int px = qBound(8, pixelSize, 16);
    const qreal sp = qBound<qreal>(0.0, spacing, 0.6);
    QFont font(g_familySemi);
    font.setPixelSize(px);
    font.setBold(bold);
    font.setLetterSpacing(QFont::AbsoluteSpacing, sp);
    font.setStyleStrategy(QFont::NoAntialias);
    return font;
}

QColor mix(const QColor& a, const QColor& b, int percent)
{
    const qreal t = qBound(0, percent, 100) / 100.0;
    return QColor(int(a.red() + (b.red() - a.red()) * t), int(a.green() + (b.green() - a.green()) * t),
                  int(a.blue() + (b.blue() - a.blue()) * t));
}

QPixmap pixelPixmap(const QStringList& glyph, const QColor& color, int scale)
{
    int columns = 0;
    for (const QString& row : glyph)
        columns = qMax(columns, int(row.size()));
    if (columns == 0 || glyph.isEmpty() || scale <= 0)
        return {};

    // One empty pixel all around for the hard outline; keeps the silhouette
    // readable on every background without antialiasing.
    const QSize size((columns + 2) * scale, (glyph.size() + 2) * scale);
    QPixmap pixmap(size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(Qt::NoPen);

    const auto filled = [&](int x, int y) {
        if (y < 0 || y >= glyph.size())
            return false;
        const QString& line = glyph.at(y);
        return x >= 0 && x < line.size() && line.at(x) == QLatin1Char('#');
    };

    for (int y = 0; y < glyph.size(); ++y) {
        for (int x = 0; x < columns; ++x) {
            if (!filled(x, y))
                continue;
            if (!filled(x - 1, y) || !filled(x + 1, y) || !filled(x, y - 1) || !filled(x, y + 1))
                plot(painter, x + 1, y + 1, scale, Ink);
        }
    }
    for (int y = 0; y < glyph.size(); ++y) {
        for (int x = 0; x < columns; ++x) {
            if (filled(x, y))
                plot(painter, x + 1, y + 1, scale, color);
        }
    }
    return pixmap;
}

QPixmap pixelArt(const QStringList& rows, const QColor& body, int scale)
{
    int columns = 0;
    for (const QString& row : rows)
        columns = qMax(columns, int(row.size()));
    if (columns == 0 || rows.isEmpty() || scale <= 0)
        return {};

    QPixmap pixmap(columns * scale, rows.size() * scale);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(Qt::NoPen);

    for (int y = 0; y < rows.size(); ++y) {
        const QString& line = rows.at(y);
        for (int x = 0; x < line.size(); ++x) {
            switch (line.at(x).toLatin1()) {
                case '#':
                    plot(painter, x, y, scale, body);
                    break;
                case '+':
                    plot(painter, x, y, scale, mix(body, QColor(Qt::white), 55));
                    break;
                case '-':
                    plot(painter, x, y, scale, mix(body, QColor(Qt::black), 40));
                    break;
                case 'o':
                    plot(painter, x, y, scale, Ink);
                    break;
                case 'g':
                    plot(painter, x, y, scale, QColor(0x5B, 0xD1, 0x6B));
                    break;
                case 'G':
                    plot(painter, x, y, scale, QColor(0x2C, 0x7A, 0x3A));
                    break;
                default:
                    break;
            }
        }
    }
    return pixmap;
}

QIcon pixelIcon(const QStringList& glyph, const QColor& color)
{
    QIcon icon;
    icon.addPixmap(pixelPixmap(glyph, color, 1));
    icon.addPixmap(pixelPixmap(glyph, color, 2));
    icon.addPixmap(pixelPixmap(glyph, color, 3));
    return icon;
}

void paintTelegram(QPainter& painter, const QRectF& area, const QColor& color)
{
    // Tip at the upper right, fold below it, tail fin at the lower left.
    const auto x = [&area](qreal value) { return area.left() + value * area.width(); };
    const auto y = [&area](qreal value) { return area.top() + value * area.height(); };

    QPainterPath plane;
    plane.moveTo(x(0.95), y(0.06));
    plane.lineTo(x(0.04), y(0.49));
    plane.lineTo(x(0.35), y(0.59));
    plane.lineTo(x(0.38), y(0.90));
    plane.lineTo(x(0.58), y(0.70));
    plane.lineTo(x(0.81), y(0.92));
    plane.closeSubpath();

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillPath(plane, color);
    painter.restore();
}

void bevel(QPainter& painter, const QRect& rect, const QColor& fill, const QColor& border, const QColor& highlight,
           const QColor& shadow)
{
    if (rect.width() <= 0 || rect.height() <= 0)
        return;

    painter.fillRect(rect, border);
    const QRect inner = rect.adjusted(2, 2, -2, -2);
    if (inner.width() <= 0 || inner.height() <= 0)
        return;

    painter.fillRect(inner, fill);
    painter.fillRect(QRect(inner.left(), inner.top(), inner.width(), 1), highlight);
    painter.fillRect(QRect(inner.left(), inner.top(), 1, inner.height()), highlight);
    painter.fillRect(QRect(inner.left(), inner.bottom(), inner.width(), 1), shadow);
    painter.fillRect(QRect(inner.right(), inner.top(), 1, inner.height()), shadow);
}

void inset(QPainter& painter, const QRect& rect, const QColor& fill, const QColor& border)
{
    painter.fillRect(rect, border);
    const QRect inner = rect.adjusted(2, 2, -2, -2);
    if (inner.width() <= 0 || inner.height() <= 0)
        return;
    painter.fillRect(inner, fill);
    painter.fillRect(QRect(inner.left(), inner.top(), inner.width(), 1), mix(fill, Qt::black, 55));
    painter.fillRect(QRect(inner.left(), inner.top(), 1, inner.height()), mix(fill, Qt::black, 55));
}

QIcon appIcon()
{
    // The .ico carries 16/24/32/48/64/128/256 pixel frames, so Windows picks a
    // crisp one for the tray, the taskbar and the window instead of resampling.
    QIcon icon(QStringLiteral(":/berry/app-icon.ico"));
    if (icon.isNull())
        icon = QIcon(QStringLiteral(":/berry/app-icon.png"));
    if (icon.isNull())
        icon = QIcon(QStringLiteral(":/berry/berry.png"));
    return icon;
}

QString elide(const QString& text, int pixelSize, qreal spacing, int maxWidth)
{
    if (maxWidth <= 0 || text.isEmpty())
        return text;
    const QFontMetrics metrics(spaced(pixelSize, spacing, true));
    return metrics.elidedText(text, Qt::ElideRight, maxWidth);
}

void applyApplication()
{
    init();
    if (auto* app = qApp) {
        // Force a flat dark base so no Prism/Breeze theme flashes through.
        if (QStyleFactory::create(QStringLiteral("Fusion")))
            app->setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        QPalette pal;
        pal.setColor(QPalette::Window, Bg);
        pal.setColor(QPalette::WindowText, Fg);
        pal.setColor(QPalette::Base, BgDeep);
        pal.setColor(QPalette::AlternateBase, Surface);
        pal.setColor(QPalette::Text, Fg);
        pal.setColor(QPalette::Button, Surface);
        pal.setColor(QPalette::ButtonText, Fg);
        pal.setColor(QPalette::Highlight, Accent);
        pal.setColor(QPalette::HighlightedText, OnAccent);
        pal.setColor(QPalette::ToolTipBase, Surface);
        pal.setColor(QPalette::ToolTipText, Fg);
        pal.setColor(QPalette::Disabled, QPalette::WindowText, Dim);
        pal.setColor(QPalette::Disabled, QPalette::Text, Dim);
        app->setPalette(pal);
        app->setFont(regular(12));
        app->setWindowIcon(appIcon());
        // Never show native file dialogs: the Qt dialog is styled pixel-wise.
        app->setStyleSheet(app->styleSheet() + QStringLiteral(R"(
QDialog, QMessageBox, QFileDialog, QWizard, QProgressDialog { background: #0A0D12; color: #F2F5FA; border: 2px solid #333D4E; }
QDialog QLabel, QMessageBox QLabel { color: #F2F5FA; }
QDialog QPushButton, QMessageBox QPushButton, QFileDialog QPushButton {
  background: #151A22; border: 2px solid #333D4E; color: #F2F5FA; padding: 6px 12px; min-height: 24px; }
QDialog QPushButton:hover, QMessageBox QPushButton:hover, QFileDialog QPushButton:hover { background: #1D2430; border-color: #FF4F79; }
QDialog QPushButton:pressed, QMessageBox QPushButton:pressed { background: #0A0D12; }
QDialog QPushButton:disabled, QMessageBox QPushButton:disabled { color: #4C5566; }
QMenu { background: #151A22; border: 2px solid #333D4E; color: #F2F5FA; padding: 4px; }
QMenu::item { padding: 5px 22px 5px 18px; }
QMenu::item:selected, QMenu::item:hover { background: #FF4F79; color: #14070C; }
QMenu::separator { height: 1px; background: #222A37; margin: 4px 8px; }
QMenu::disabled { color: #4C5566; }
QToolBar { background: #0C1016; border: none; border-bottom: 2px solid #1A212B; spacing: 4px; padding: 4px; }
QToolBar QToolButton { background: transparent; border: 1px solid transparent; padding: 5px; }
QToolBar QToolButton:hover { background: #151A22; border: 1px solid #333D4E; }
QStatusBar { background: #0C1016; color: #8C98AC; border-top: 2px solid #1A212B; }
QDockWidget { background: #0A0D12; border: 2px solid #333D4E; titlebar-close-icon: none; }
QDockWidget::title { background: #0C1016; color: #F2F5FA; padding: 4px; text-align: left; }
QGroupBox { border: 2px solid #333D4E; margin-top: 10px; padding-top: 8px; color: #F2F5FA; }
QGroupBox::title { subcontrol-origin: margin; left: 8px; }
QComboBox, QTextEdit, QPlainTextEdit, QTextBrowser, QTreeWidget, QTreeView, QTableWidget, QTableView {
  background: #07090D; border: 2px solid #333D4E; color: #F2F5FA; selection-background-color: #FF4F79; selection-color: #14070C; }
QComboBox:focus, QTextEdit:focus, QPlainTextEdit:focus { border-color: #FF4F79; }
QComboBox QAbstractItemView { background: #151A22; border: 2px solid #333D4E; selection-background-color: #FF4F79; selection-color: #14070C; }
QRadioButton, QCheckBox { color: #F2F5FA; }
QRadioButton::indicator, QCheckBox::indicator { width: 14px; height: 14px; border: 2px solid #333D4E; background: #07090D; }
QRadioButton::indicator:checked, QCheckBox::indicator:checked { background: #FF4F79; }
QTabWidget::pane { border: 2px solid #333D4E; background: #0A0D12; }
QTabBar::tab { background: #0C1016; color: #9EA9BC; border: 2px solid #222A37; padding: 6px 12px; }
QTabBar::tab:selected { background: #151A22; color: #F2F5FA; border-bottom-color: #151A22; }
QHeaderView::section { background: #0C1016; color: #9EA9BC; border: 1px solid #222A37; padding: 4px; }
QSplitter::handle { background: #222A37; }
QToolTip { background: #151A22; color: #F2F5FA; border: 2px solid #FF4F79; padding: 4px 6px; }
QFileSystemModel { background: #07090D; color: #F2F5FA; }
)"));
    }
}

void apply(QWidget* root)
{
    init();
    root->setFont(regular(12));
    root->setStyleSheet(QStringLiteral(R"(
/* ---------- base ---------- */
QWidget#berryRoot { background: #0A0D12; color: #F2F5FA; }
QStackedWidget#berryPages { background: transparent; }
QWidget#berryPage { background: transparent; }
QWidget { color: #F2F5FA; }
QLabel { background: transparent; color: #F2F5FA; }
QLabel:disabled { color: #4C5566; }

QLabel#berryDisplay { color: #F7FAFF; font-size: 16px; }
QLabel#berryTitle { color: #F7FAFF; font-size: 14px; }
QLabel#berryH1 { color: #F5F8FD; font-size: 12px; }
QLabel#berryH2 { color: #F5F8FD; font-size: 10px; }
QLabel#berryCaption { color: #7E8A9E; font-size: 8px; }
QLabel#berryMuted { color: #A7B2C4; font-size: 10px; }
QLabel#berryDim { color: #77839A; font-size: 9px; }
QLabel#berryMono { color: #B9C4D6; font-size: 9px; }
QLabel#berryAccent { color: #FF5D86; }
QLabel#berryOk { color: #52E39C; }
QLabel#berryWarn { color: #FFC24B; }
QLabel#berryErr { color: #FF6B6B; }

/* ---------- top bar / tabs / footer ---------- */
QFrame#berryTopbar { background: #0C1016; border: none; border-bottom: 2px solid #1A212B; }
QFrame#berryTopbarLine { background: #FF4F79; border: none; min-height: 2px; max-height: 2px; }
QWidget#berryTabStrip { background: #080B0F; border-bottom: 2px solid #1A212B; }
QFrame#berryFooter { background: #0C1016; border: none; border-top: 2px solid #1A212B; }
QLabel#berryFooterCell { color: #8C98AC; font-size: 10px; padding: 0px 10px; }

/* ---------- text inputs ---------- */
QLineEdit, QSpinBox {
    background: #07090D;
    border: 2px solid #333D4E;
    color: #F2F5FA;
    padding: 7px 9px;
    min-height: 20px;
    selection-background-color: #FF4F79;
    selection-color: #14070C;
}
QLineEdit:focus, QSpinBox:focus { border-color: #FF4F79; }
QLineEdit:disabled, QSpinBox:disabled { color: #4C5566; border-color: #262E3C; }
QSpinBox::up-button, QSpinBox::down-button { width: 0; height: 0; border: none; }
QSpinBox::up-arrow, QSpinBox::down-arrow { width: 0; height: 0; }

/* ---------- scrolling ---------- */
QScrollArea { background: transparent; border: none; }
QScrollArea > QWidget > QWidget { background: transparent; }
QScrollBar:vertical { background: #07090D; width: 10px; margin: 0; }
QScrollBar::handle:vertical { background: #2A3342; min-height: 28px; }
QScrollBar::handle:vertical:hover { background: #FF4F79; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
QScrollBar:horizontal { background: #07090D; height: 10px; margin: 0; }
QScrollBar::handle:horizontal { background: #2A3342; min-width: 28px; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }

/* ---------- lists ---------- */
QListWidget {
    background: #07090D;
    border: 2px solid #333D4E;
    color: #F2F5FA;
    outline: none;
}
QListWidget::item { padding: 6px 8px; border-bottom: 1px solid #141A23; }
QListWidget::item:hover { background: #151A22; }
QListWidget::item:selected { background: #2A1020; color: #FFD7E1; }

/* ---------- tooltips and menus ---------- */
QToolTip { background: #151A22; color: #F2F5FA; border: 2px solid #FF4F79; padding: 4px 6px; }
QMenu { background: #151A22; border: 2px solid #333D4E; }
QMenu::item { padding: 5px 18px; }
QMenu::item:selected { background: #FF4F79; color: #14070C; }
)")
    );
}

}  // namespace berry::theme
