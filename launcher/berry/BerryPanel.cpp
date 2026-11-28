// SPDX-License-Identifier: GPL-3.0-only
/*
 *  BerryTrip Launcher - single server build of Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "berry/BerryPanel.h"

#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFutureWatcher>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressDialog>
#include <QWizard>
#include <QMainWindow>
#include <QMenu>
#include <QMouseEvent>
#include <QMenuBar>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPainterPath>
#include <QPair>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStorageInfo>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent>

#include "Application.h"
#include "LaunchMode.h"
#include "berry/BerryAuth.h"
#include "berry/BerryConfig.h"
#include "berry/BerryInstance.h"
#include "berry/BerryPackSync.h"
#include "berry/BerryPing.h"
#include "berry/BerrySkin.h"
#include "berry/BerrySkin3D.h"
#include "berry/BerryTheme.h"
#include "berry/BerryTweaks.h"
#include "berry/BerryUpdater.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/launch/MinecraftTarget.h"
#include "settings/SettingsObject.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace berry {

namespace {

constexpr auto SiteUrl = "https://evilvalue.github.io/berry/";
constexpr auto TelegramUrl = "https://t.me/killberriesbot";
constexpr auto ReleasesUrl = "https://github.com/evilvalue/prism-berry/releases/latest";
constexpr auto SourcesUrl = "https://github.com/evilvalue/prism-berry";
constexpr auto UpstreamUrl = "https://github.com/PrismLauncher/PrismLauncher";

class OverlayFilter : public QObject {
   public:
    OverlayFilter(QWidget* overlay, QObject* parent) : QObject(parent), m_overlay(overlay) {}

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Resize) {
            if (auto* widget = qobject_cast<QWidget*>(watched))
                m_overlay->setGeometry(widget->rect());
        }
        return QObject::eventFilter(watched, event);
    }

   private:
    QWidget* m_overlay;
};

/// Drags the frameless window from the pixel top bar; double-click toggles maximize.
class TitleDragFilter : public QObject {
   public:
    explicit TitleDragFilter(Panel* panel, QObject* parent = nullptr) : QObject(parent), m_panel(panel) {}
   protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* e = static_cast<QMouseEvent*>(event);
            if (e->button() == Qt::LeftButton) { m_drag = e->globalPosition().toPoint() - m_panel->window()->pos(); m_down = true; }
        } else if (event->type() == QEvent::MouseMove && m_down) {
            auto* e = static_cast<QMouseEvent*>(event);
            m_panel->window()->move(e->globalPosition().toPoint() - m_drag);
            return true;
        } else if (event->type() == QEvent::MouseButtonRelease) {
            m_down = false;
        } else if (event->type() == QEvent::MouseButtonDblClick) {
            auto* w = m_panel->window();
            if (w->isMaximized()) w->showNormal(); else w->showMaximized();
            return true;
        }
        return QObject::eventFilter(watched, event);
    }
   private:
    Panel* m_panel;
    QPoint m_drag;
    bool m_down = false;
};

class PopupFilter : public QObject {
   public:
    PopupFilter(Panel* panel, QObject* parent) : QObject(parent), m_panel(panel) {}

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Show) {
            if (auto* box = qobject_cast<QMessageBox*>(watched)) {
                m_panel->notice(box->text().isEmpty() ? box->windowTitle() : box->text());
                box->hide();
                return true;
            }
            if (qobject_cast<QProgressDialog*>(watched) || qobject_cast<QWizard*>(watched)) {
                static_cast<QWidget*>(watched)->hide();
                return true;
            }
        }
        return QObject::eventFilter(watched, event);
    }

   private:
    Panel* m_panel;
};

/// Keeps Prism's own chrome (menu bar, toolbars, docks, status bar) hidden when
/// MainWindow or a theme tries to reshow it. Widgets are hidden, never deleted:
/// MainWindow still holds pointers to them.
class PrismChromeFilter : public QObject {
   public:
    PrismChromeFilter(QWidget* window, QObject* parent) : QObject(parent), m_window(window) {}

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Show) {
            auto* widget = qobject_cast<QWidget*>(watched);
            if (widget && widget != m_window && widget->isWindow() == false &&
                (qobject_cast<QToolBar*>(widget) || qobject_cast<QDockWidget*>(widget) || qobject_cast<QStatusBar*>(widget) ||
                 qobject_cast<QMenuBar*>(widget))) {
                widget->hide();
                return true;
            }
        }
        return QObject::eventFilter(watched, event);
    }

   private:
    QWidget* m_window;
};

/// The close button hides the launcher instead of quitting it, so a running
/// download or a just-launched game is not cut off. The tray icon brings it back.
class TrayFilter : public QObject {
   public:
    TrayFilter(QWidget* window, QSystemTrayIcon* tray) : QObject(window), m_window(window), m_tray(tray) {}

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == m_window && event->type() == QEvent::Close) {
            event->ignore();
            m_window->hide();
            if (!m_hinted) {
                m_hinted = true;
                m_tray->showMessage(QObject::tr("BerryTrip свёрнут в трей"),
                                    QObject::tr("Лаунчер продолжает работать. Клик по значку вернёт окно."),
                                    QSystemTrayIcon::Information, 4000);
            }
            return true;
        }
        return QObject::eventFilter(watched, event);
    }

   private:
    QWidget* m_window;
    QSystemTrayIcon* m_tray;
    bool m_hinted = false;
};

// --------------------------------------------------------------- pixel icons
// Every glyph is a 16x16 grid drawn at 1:1. Shapes use whole pixels only:
// no antialiasing, no half-pixel outlines, one light source (top-left).

const QStringList& gHome()
{
    static const QStringList g{ QStringLiteral("................"), QStringLiteral(".......##......."),
                                QStringLiteral("......####......"), QStringLiteral(".....######....."),
                                QStringLiteral("....########...."), QStringLiteral("...##########..."),
                                QStringLiteral("..############.."), QStringLiteral(".....######....."),
                                QStringLiteral(".....#....#....."), QStringLiteral(".....#.##.#....."),
                                QStringLiteral(".....#.##.#....."), QStringLiteral(".....#....#....."),
                                QStringLiteral(".....######....."), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................") };
    return g;
}

const QStringList& gAccount()
{
    static const QStringList g{ QStringLiteral("................"), QStringLiteral("......####......"),
                                QStringLiteral(".....######....."), QStringLiteral(".....######....."),
                                QStringLiteral(".....######....."), QStringLiteral("......####......"),
                                QStringLiteral("................"), QStringLiteral("....########...."),
                                QStringLiteral("..############.."), QStringLiteral("..###......###.."),
                                QStringLiteral("..###......###.."), QStringLiteral("..###......###.."),
                                QStringLiteral("..###......###.."), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................") };
    return g;
}

const QStringList& gSkin()
{
    static const QStringList g{ QStringLiteral("................"), QStringLiteral("....##....##...."),
                                QStringLiteral("...####..####..."), QStringLiteral("..############.."),
                                QStringLiteral(".##############."), QStringLiteral(".###..####..###."),
                                QStringLiteral("...##..##..##..."), QStringLiteral("...##..##..##..."),
                                QStringLiteral("...##..##..##..."), QStringLiteral("...##..##..##..."),
                                QStringLiteral("...##..##..##..."), QStringLiteral("...##########..."),
                                QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................") };
    return g;
}

const QStringList& gSettings()
{
    static const QStringList g{ QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral(".#############.."), QStringLiteral(".....##........."),
                                QStringLiteral(".....##........."), QStringLiteral("................"),
                                QStringLiteral("..#############."), QStringLiteral("........###....."),
                                QStringLiteral("........###....."), QStringLiteral("................"),
                                QStringLiteral(".#############.."), QStringLiteral("....##.........."),
                                QStringLiteral("....##.........."), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................") };
    return g;
}

const QStringList& gDiagnostics()
{
    static const QStringList g{ QStringLiteral("................"), QStringLiteral(".....######....."),
                                QStringLiteral("...##......##..."), QStringLiteral("..#..........#.."),
                                QStringLiteral("..#....##....#.."), QStringLiteral("..#....##....#.."),
                                QStringLiteral("..#...####...#.."), QStringLiteral("..#..##..##..#.."),
                                QStringLiteral("...#........#..."), QStringLiteral("....########...."),
                                QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................") };
    return g;
}

const QStringList& gInfo()
{
    static const QStringList g{ QStringLiteral("................"), QStringLiteral(".......##......."),
                                QStringLiteral(".......##......."), QStringLiteral("................"),
                                QStringLiteral(".....######....."), QStringLiteral(".......##......."),
                                QStringLiteral(".......##......."), QStringLiteral(".......##......."),
                                QStringLiteral(".......##......."), QStringLiteral(".....######....."),
                                QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................") };
    return g;
}

const QStringList& gPlay()
{
    static const QStringList g{ QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("......##........"), QStringLiteral("......####......"),
                                QStringLiteral("......######...."), QStringLiteral("......#######..."),
                                QStringLiteral("......#########."), QStringLiteral("......#########."),
                                QStringLiteral("......########.."), QStringLiteral("......######...."),
                                QStringLiteral("......####......"), QStringLiteral("......##........"),
                                QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................") };
    return g;
}

const QStringList& gChevron()
{
    static const QStringList g{ QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("........##......"), QStringLiteral(".......##......."),
                                QStringLiteral("......##........"), QStringLiteral(".....##........."),
                                QStringLiteral(".....##........."), QStringLiteral("......##........"),
                                QStringLiteral(".......##......."), QStringLiteral("........##......"),
                                QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................") };
    return g;
}

const QStringList& gTelegram()
{
    static const QStringList g{ QStringLiteral("................"), QStringLiteral(".............##."),
                                QStringLiteral("............####"), QStringLiteral("...........#####"),
                                QStringLiteral("..........######"), QStringLiteral(".........######."),
                                QStringLiteral("........######.."), QStringLiteral(".......######..."),
                                QStringLiteral("......######...."), QStringLiteral(".....######....."),
                                QStringLiteral("....#####......."), QStringLiteral("...####........."),
                                QStringLiteral("..##............"), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................") };
    return g;
}

const QStringList& gRefresh()
{
    static const QStringList g{ QStringLiteral("................"), QStringLiteral("......####..##.."),
                                QStringLiteral("....##....####.."), QStringLiteral("...#......#####."),
                                QStringLiteral("..#........###.."), QStringLiteral("..#..........#.."),
                                QStringLiteral("..#..........#.."), QStringLiteral("...#........#..."),
                                QStringLiteral("....##....##...."), QStringLiteral("......####......"),
                                QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................") };
    return g;
}

const QStringList& gFolder()
{
    static const QStringList g{ QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("..####.........."), QStringLiteral("..#...#######..."),
                                QStringLiteral("..#.........#..."), QStringLiteral("..#.........#..."),
                                QStringLiteral("..###########..."), QStringLiteral("..#.........#..."),
                                QStringLiteral("..#.........#..."), QStringLiteral("..###########..."),
                                QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................") };
    return g;
}

const QStringList& gCopy()
{
    static const QStringList g{ QStringLiteral("................"), QStringLiteral("....#######....."),
                                QStringLiteral("....#.....#....."), QStringLiteral("..###########..."),
                                QStringLiteral("..#.....#...#..."), QStringLiteral("..#.....#...#..."),
                                QStringLiteral("..#.....#...#..."), QStringLiteral("..#.....#...#..."),
                                QStringLiteral("..###########..."), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................") };
    return g;
}

const QStringList& gCheck()
{
    static const QStringList g{ QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("............##.."), QStringLiteral("...........###.."),
                                QStringLiteral("..........###..."), QStringLiteral(".........###...."),
                                QStringLiteral("..##....###....."), QStringLiteral("..###..###......"),
                                QStringLiteral("...######......."), QStringLiteral("....####........"),
                                QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................") };
    return g;
}

const QStringList& gDownload()
{
    static const QStringList g{ QStringLiteral("................"), QStringLiteral(".......##......."),
                                QStringLiteral(".......##......."), QStringLiteral(".......##......."),
                                QStringLiteral(".......##......."), QStringLiteral(".......##......."),
                                QStringLiteral("..##...##...##.."), QStringLiteral("...##..##..##..."),
                                QStringLiteral("....########...."), QStringLiteral(".......##......."),
                                QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................") };
    return g;
}

const QStringList& gPlus()
{
    static const QStringList g{ QStringLiteral("................"), QStringLiteral(".......##......."),
                                QStringLiteral(".......##......."), QStringLiteral(".......##......."),
                                QStringLiteral("....########...."), QStringLiteral("....########...."),
                                QStringLiteral(".......##......."), QStringLiteral(".......##......."),
                                QStringLiteral(".......##......."), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................") };
    return g;
}

const QStringList& gMinus()
{
    static const QStringList g{ QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("....########...."), QStringLiteral("....########...."),
                                QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................") };
    return g;
}

const QStringList& gTrash()
{
    static const QStringList g{ QStringLiteral("................"), QStringLiteral("......####......"),
                                QStringLiteral("..############.."), QStringLiteral("....#......#...."),
                                QStringLiteral("....#......#...."), QStringLiteral("....#......#...."),
                                QStringLiteral("....#......#...."), QStringLiteral("....#......#...."),
                                QStringLiteral("....#......#...."), QStringLiteral("....########...."),
                                QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................") };
    return g;
}

const QStringList& gLogout()
{
    static const QStringList g{ QStringLiteral("................"), QStringLiteral("....####........"),
                                QStringLiteral("....#..#........"), QStringLiteral("....#..#..##...."),
                                QStringLiteral("....#..#..####.."), QStringLiteral("....#..#..######"),
                                QStringLiteral("....#..#..####.."), QStringLiteral("....#..#..##...."),
                                QStringLiteral("....####........"), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................") };
    return g;
}

const QStringList& gExternal()
{
    static const QStringList g{ QStringLiteral("................"), QStringLiteral("........######.."),
                                QStringLiteral("........#....#.."), QStringLiteral("........#....#.."),
                                QStringLiteral("........#..###.."), QStringLiteral("........#.###..."),
                                QStringLiteral("........###....."), QStringLiteral(".......###......"),
                                QStringLiteral("......###......."), QStringLiteral(".....###........"),
                                QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................") };
    return g;
}

const QStringList& gGlobe()
{
    static const QStringList g{ QStringLiteral("................"), QStringLiteral(".....######....."),
                                QStringLiteral("...##..##..##..."), QStringLiteral("..#....##....#.."),
                                QStringLiteral(".#.....##.....#."), QStringLiteral(".##############."),
                                QStringLiteral(".#.....##.....#."), QStringLiteral("..#....##....#.."),
                                QStringLiteral("...##..##..##..."), QStringLiteral(".....######....."),
                                QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................") };
    return g;
}

const QStringList& gChip()
{
    static const QStringList g{ QStringLiteral("................"), QStringLiteral("..##......##...."),
                                QStringLiteral("..############.."), QStringLiteral("..#..######..#.."),
                                QStringLiteral("..#..#....#..#.."), QStringLiteral("..#..#....#..#.."),
                                QStringLiteral("..#..#....#..#.."), QStringLiteral("..#..######..#.."),
                                QStringLiteral("..############.."), QStringLiteral("..##......##...."),
                                QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................") };
    return g;
}

const QStringList& gCoffee()
{
    static const QStringList g{ QStringLiteral("................"), QStringLiteral("....######......"),
                                QStringLiteral("...#......##...."), QStringLiteral("...#.......#...."),
                                QStringLiteral("...#......##...."), QStringLiteral("....#....##....."),
                                QStringLiteral("....######......"), QStringLiteral("................"),
                                QStringLiteral("...########....."), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................") };
    return g;
}

const QStringList& gMods()
{
    static const QStringList g{ QStringLiteral("................"), QStringLiteral("..####...####..."),
                                QStringLiteral("..#..#...#..#..."), QStringLiteral("..#..#...#..#..."),
                                QStringLiteral("..####...####..."), QStringLiteral("................"),
                                QStringLiteral("..####...####..."), QStringLiteral("..#..#...#..#..."),
                                QStringLiteral("..#..#...#..#..."), QStringLiteral("..####...####..."),
                                QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................") };
    return g;
}

const QStringList& gMonitor()
{
    static const QStringList g{ QStringLiteral("................"), QStringLiteral("..############.."),
                                QStringLiteral("..#..........#.."), QStringLiteral("..#..######..#.."),
                                QStringLiteral("..#..#....#..#.."), QStringLiteral("..#..#....#..#.."),
                                QStringLiteral("..#..######..#.."), QStringLiteral("..#..........#.."),
                                QStringLiteral("..############.."), QStringLiteral(".....##..##....."),
                                QStringLiteral("....########...."), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................") };
    return g;
}

const QStringList& gShield()
{
    static const QStringList g{ QStringLiteral("................"), QStringLiteral("....########...."),
                                QStringLiteral("...#........#..."), QStringLiteral("..#..........#.."),
                                QStringLiteral("..#..........#.."), QStringLiteral("..#..........#.."),
                                QStringLiteral("..#..........#.."), QStringLiteral("...#........#..."),
                                QStringLiteral("....##....##...."), QStringLiteral("......####......"),
                                QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................") };
    return g;
}

const QStringList& gKey()
{
    static const QStringList g{ QStringLiteral("................"), QStringLiteral(".....####......."),
                                QStringLiteral("....#....#......"), QStringLiteral("....#....#......"),
                                QStringLiteral(".....####......."), QStringLiteral("......##........"),
                                QStringLiteral("......##........"), QStringLiteral("......####......"),
                                QStringLiteral("......##........"), QStringLiteral("......####......"),
                                QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................") };
    return g;
}

const QStringList& gDot()
{
    static const QStringList g{ QStringLiteral("........"), QStringLiteral("..####.."),
                                QStringLiteral(".######."), QStringLiteral(".######."),
                                QStringLiteral(".######."), QStringLiteral("..####.."),
                                QStringLiteral("........"), QStringLiteral("........") };
    return g;
}

const QColor IconFg(0xEC, 0xF1, 0xF8);
const QColor IconDark(0x14, 0x07, 0x0C);
const QColor IconMuted(0x8C, 0x98, 0xAC);

// ------------------------------------------------------------- extra glyphs
const QStringList& gServer()
{
    static const QStringList g{ QStringLiteral("................"), QStringLiteral("..############.."),
                                QStringLiteral("..#..........#.."), QStringLiteral("..#.##.##.##.#.."),
                                QStringLiteral("..#..........#.."), QStringLiteral("..############.."),
                                QStringLiteral("..#..........#.."), QStringLiteral("..#.##.##.##.#.."),
                                QStringLiteral("..#..........#.."), QStringLiteral("..############.."),
                                QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................") };
    return g;
}

const QStringList& gWarn()
{
    static const QStringList g{ QStringLiteral("................"), QStringLiteral(".......##......."),
                                QStringLiteral("......####......"), QStringLiteral("......####......"),
                                QStringLiteral(".....##..##....."), QStringLiteral(".....##..##....."),
                                QStringLiteral("....##....##...."), QStringLiteral("....##....##...."),
                                QStringLiteral("...##......##..."), QStringLiteral("...##########..."),
                                QStringLiteral("...##......##..."), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................") };
    return g;
}

const QStringList& gClock()
{
    static const QStringList g{ QStringLiteral("................"), QStringLiteral(".....######....."),
                                QStringLiteral("...##......##..."), QStringLiteral("..#....##....#.."),
                                QStringLiteral("..#....##....#.."), QStringLiteral(".#.....##.....#."),
                                QStringLiteral(".#.....##.....#."), QStringLiteral(".#.....##.....#."),
                                QStringLiteral(".#.....###....#."), QStringLiteral("..#..........#.."),
                                QStringLiteral("...##......##..."), QStringLiteral(".....######....."),
                                QStringLiteral("................"), QStringLiteral("................"),
                                QStringLiteral("................"), QStringLiteral("................") };
    return g;
}

const QStringList& gLogoSprite()
{
    static const QStringList g{
        QStringLiteral("................"),
        QStringLiteral(".......gg......."),
        QStringLiteral("......gGGg......"),
        QStringLiteral("....gGg..gGg...."),
        QStringLiteral("...gG......Gg..."),
        QStringLiteral(".....oooooo....."),
        QStringLiteral("....o######o...."),
        QStringLiteral("...o##+#####o..."),
        QStringLiteral("..o#+#######-o.."),
        QStringLiteral("..o##########o.."),
        QStringLiteral("..o##########o.."),
        QStringLiteral("..o-##-###--#o.."),
        QStringLiteral("...o-######-o..."),
        QStringLiteral("....o######o...."),
        QStringLiteral(".....oooooo....."),
        QStringLiteral("................"),
    };
    return g;
}

// ------------------------------------------------------------------ painting
void dither(QPainter& painter, const QRect& area, const QColor& base, const QColor& dot, int step)
{
    painter.fillRect(area, base);
    for (int y = area.top(); y < area.bottom(); y += step) {
        for (int x = area.left() + ((y / step) % 2) * (step / 2); x < area.right(); x += step)
            painter.fillRect(QRect(x, y, 1, 1), dot);
    }
}

QColor hoverFill(const QColor& fill) { return theme::mix(fill, QColor(Qt::white), 12); }
QColor pressFill(const QColor& fill) { return theme::mix(fill, QColor(Qt::black), 18); }

class PixelButton : public QPushButton {
   public:
    enum Variant { Normal, Primary, Ghost, Segment, Icon };

    PixelButton(const QString& text, Variant variant, const QStringList& glyph, QWidget* parent)
        : QPushButton(text, parent), m_variant(variant), m_glyph(glyph)
    {
        setCursor(Qt::PointingHandCursor);
        setAutoDefault(false);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_Hover, true);
        setFlat(true);
    }

    void setGlyph(const QStringList& glyph)
    {
        m_glyph = glyph;
        update();
    }

    QSize sizeHint() const override
    {
        const QFontMetrics metrics(font());
        const int textWidth = text().isEmpty() ? 0 : metrics.horizontalAdvance(text());
        if (m_variant == Icon)
            return QSize(40, 40);
        const int iconWidth = m_glyph.isEmpty() ? 0 : 22;
        const int height = m_variant == Segment ? 32 : m_variant == Ghost ? 36 : 40;
        // cap so long labels never blow the layout; paint elides the rest
        return QSize(qMin(textWidth + iconWidth + 32, 360), height);
    }

    QSize minimumSizeHint() const override { return sizeHint(); }

   protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setRenderHint(QPainter::TextAntialiasing, false);

        const bool enabled = isEnabled();
        const bool hovered = underMouse() && enabled;
        const bool pressed = isDown() && enabled;
        const bool checked = isCheckable() && isChecked();

        QColor fill = theme::Surface;
        QColor border = theme::Line;
        QColor highlight = theme::mix(theme::Line, Qt::white, 28);
        QColor shadow = theme::Ink;
        QColor label = theme::Fg;

        switch (m_variant) {
            case Primary:
                fill = theme::Accent;
                border = theme::AccentDeep;
                highlight = theme::AccentHi;
                shadow = theme::AccentDark;
                label = theme::OnAccent;
                break;
            case Ghost:
                fill = QColor(Qt::transparent);
                border = QColor(Qt::transparent);
                label = theme::Muted;
                break;
            case Segment:
                fill = theme::SurfaceLo;
                label = theme::Muted;
                if (checked) {
                    fill = theme::Accent;
                    border = theme::AccentDeep;
                    highlight = theme::AccentHi;
                    shadow = theme::AccentDark;
                    label = theme::OnAccent;
                }
                break;
            default:
                break;
        }

        if (!enabled) {
            fill = theme::mix(fill, theme::Bg, 55);
            border = theme::mix(border, theme::Bg, 45);
            highlight = theme::mix(highlight, theme::Bg, 45);
            label = theme::mix(label, theme::Bg, 55);
        } else if (hovered) {
            fill = hoverFill(fill);
            label = m_variant == Primary ? theme::OnAccent : (m_variant == Ghost ? theme::AccentHi : theme::Fg);
        }

        // Reserve the bottom/right 3px so the hard drop shadow is never
        // clipped by the widget bounds.
        QRect body = rect().adjusted(0, 0, -3, -3);
        const bool quiet = m_variant == Ghost && !hovered && !pressed;

        // Hard drop shadow: a whole 3px block, never a blur.
        if (!quiet && enabled)
            painter.fillRect(body.translated(3, 3), theme::Ink);
        if (pressed) {
            body.translate(2, 2);
            fill = pressFill(fill);
        }

        if (m_variant == Ghost) {
            if (hovered || pressed) {
                painter.fillRect(body, theme::Surface);
                theme::bevel(painter, body, theme::Surface, theme::LineSoft, theme::mix(theme::LineSoft, Qt::white, 20),
                             theme::Ink);
            }
            label = hovered ? theme::Fg : (pressed ? theme::AccentHi : theme::Muted);
        } else {
            painter.fillRect(body, theme::Ink);
            const QRect inner = body.adjusted(1, 1, -1, -1);
            theme::bevel(painter, inner, fill, border, highlight, shadow);
            if (m_variant == Primary) {
                // Pixel-art sheen: a single 2px light row two pixels inside.
                painter.fillRect(QRect(inner.left() + 3, inner.top() + 3, inner.width() - 6, 2),
                                 theme::mix(fill, Qt::white, 30));
            }
            if (m_variant == Segment && checked)
                painter.fillRect(QRect(inner.left() + 4, inner.bottom() - 4, inner.width() - 8, 2), theme::AccentHi);
        }

        int x = body.left() + (m_variant == Icon ? 0 : 12);
        if (!m_glyph.isEmpty()) {
            const QPixmap pixmap = theme::pixelPixmap(m_glyph, label, 1);
            const int gx = m_variant == Icon ? body.center().x() - pixmap.width() / 2 : x;
            const int gy = body.center().y() - pixmap.height() / 2;
            painter.drawPixmap(gx, gy, pixmap);
            if (m_variant != Icon)
                x += pixmap.width() + 8;
        }

        if (!text().isEmpty() && m_variant != Icon) {
            painter.setFont(font());
            painter.setPen(label);
            const QString shown = QFontMetrics(font()).elidedText(text(), Qt::ElideRight, body.right() - x - 6);
            painter.drawText(QRect(x, body.top(), body.right() - x - 6, body.height()), Qt::AlignVCenter | Qt::AlignLeft,
                             shown);
        }

        if (hasFocus()) {
            painter.setPen(theme::AccentHi);
            painter.drawRect(rect().adjusted(0, 0, -1, -1));
        }
    }

   private:
    Variant m_variant;
    QStringList m_glyph;
};

class PixelFrame : public QFrame {
   public:
    enum Style { Card, Hero, Inset, Banner, Plain };

    explicit PixelFrame(Style style = Card, QWidget* parent = nullptr) : QFrame(parent), m_style(style) {}

    Style style() const { return m_style; }

   protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, false);
        const QRect area = rect();
        if (area.width() < 6 || area.height() < 6)
            return;

        painter.fillRect(area, theme::Ink);
        const QRect inner = area.adjusted(1, 1, -1, -1);

        switch (m_style) {
            case Hero:
                dither(painter, inner.adjusted(2, 2, -2, -2), QColor(0x18, 0x13, 0x1E), QColor(0x1C, 0x16, 0x24), 4);
                theme::bevel(painter, inner, QColor(0x18, 0x13, 0x1E), theme::AccentDeep, theme::AccentHi,
                             theme::AccentDark);
                painter.fillRect(QRect(inner.left() + 2, inner.top() + 2, inner.width() - 4, 3), theme::Accent);
                break;
            case Banner:
                dither(painter, inner.adjusted(2, 2, -2, -2), QColor(0x23, 0x12, 0x1B), QColor(0x2B, 0x17, 0x21), 4);
                theme::bevel(painter, inner, QColor(0x23, 0x12, 0x1B), theme::AccentDeep, theme::Accent,
                             theme::AccentDark);
                break;
            case Inset:
                painter.fillRect(inner, theme::LineSoft);
                painter.fillRect(inner.adjusted(2, 2, -2, -2), theme::BgDeep);
                painter.fillRect(QRect(inner.left() + 2, inner.top() + 2, inner.width() - 4, 1), theme::Ink);
                painter.fillRect(QRect(inner.left() + 2, inner.top() + 2, 1, inner.height() - 4), theme::Ink);
                break;
            case Plain:
                break;
            case Card:
            default:
                theme::bevel(painter, inner, theme::Surface, theme::Line, theme::mix(theme::Line, Qt::white, 26),
                             theme::Ink);
                break;
        }
    }

   private:
    Style m_style;
};

class PixelProgress : public QProgressBar {
   public:
    explicit PixelProgress(QWidget* parent = nullptr) : QProgressBar(parent)
    {
        setTextVisible(false);
        setFixedHeight(16);
        setAttribute(Qt::WA_Hover, false);
        m_tick = new QTimer(this);
        m_tick->setInterval(90);
        connect(m_tick, &QTimer::timeout, this, [this] {
            m_offset = (m_offset + 1) % 4;
            update();
        });
        m_tick->start();
    }

   protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, false);
        const QRect area = rect();
        painter.fillRect(area, theme::Ink);
        theme::inset(painter, area.adjusted(1, 1, -1, -1), theme::BgDeep, theme::LineSoft);

        const QRect track = area.adjusted(4, 4, -4, -4);
        if (track.width() <= 0 || track.height() <= 0)
            return;

        const int block = 6;
        const int gap = 2;
        const int cells = qMax(1, (track.width() + gap) / (block + gap));

        int filled = 0;
        if (maximum() <= 0) {
            // Busy: a marching band of four blocks instead of a spinner.
            for (int i = 0; i < cells; ++i) {
                if (i % 3 == m_offset % 3)
                    painter.fillRect(QRect(track.left() + i * (block + gap), track.top(), block, track.height()),
                                     theme::Accent);
            }
            return;
        }

        filled = qBound(0, int(qint64(cells) * value() / qMax(1, maximum())), cells);
        for (int i = 0; i < cells; ++i) {
            const QRect cell(track.left() + i * (block + gap), track.top(), block, track.height());
            if (i < filled) {
                painter.fillRect(cell, theme::Accent);
                painter.fillRect(QRect(cell.left(), cell.top(), cell.width(), 1), theme::AccentHi);
            } else {
                painter.fillRect(cell, theme::SurfaceLo);
            }
        }
    }

   private:
    QTimer* m_tick = nullptr;
    int m_offset = 0;
};

class PixelCheck : public QCheckBox {
   public:
    explicit PixelCheck(const QString& text, QWidget* parent = nullptr) : QCheckBox(text, parent)
    {
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);
        setFont(theme::regular(12));
        setMinimumHeight(26);
    }

   protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setRenderHint(QPainter::TextAntialiasing, false);

        const bool hovered = underMouse() && isEnabled();
        const bool on = isChecked();
        const QRect box(0, (height() - 16) / 2, 16, 16);

        painter.fillRect(box, theme::Ink);
        const QRect inner = box.adjusted(1, 1, -1, -1);
        theme::bevel(painter, inner, on ? theme::Accent : theme::BgDeep, on ? theme::AccentDeep : theme::Line,
                     on ? theme::AccentHi : theme::mix(theme::Line, Qt::white, 20), theme::Ink);

        if (on) {
            painter.setPen(QPen(theme::OnAccent, 2));
            const int x = inner.left() + 3;
            const int y = inner.top() + 7;
            painter.drawLine(x, y, x + 2, y + 3);
            painter.drawLine(x + 2, y + 3, x + 7, y - 3);
        }

        painter.setFont(font());
        painter.setPen(isEnabled() ? (hovered ? theme::Fg : theme::Muted) : theme::Dim);
        painter.drawText(QRect(24, 0, width() - 24, height()), Qt::AlignVCenter | Qt::AlignLeft, text());
    }
};

class PixelTab : public QPushButton {
   public:
    PixelTab(const QString& label, const QStringList& glyph, QWidget* parent)
        : QPushButton(label, parent), m_glyph(glyph)
    {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_Hover, true);
        setFont(theme::spaced(11, 0.8, true));
        setFixedHeight(52);
        setMinimumWidth(132);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

   protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setRenderHint(QPainter::TextAntialiasing, false);

        const bool active = isChecked();
        const bool hovered = underMouse();
        const QRect area = rect();

        if (active) {
            painter.fillRect(area, QColor(0x16, 0x1C, 0x26));
            painter.fillRect(QRect(area.left(), area.top(), area.width(), 3), theme::Accent);
            painter.fillRect(QRect(area.left(), area.top() + 3, area.width(), 1), theme::AccentDeep);
        } else if (hovered) {
            painter.fillRect(area, QColor(0x0F, 0x13, 0x19));
            painter.fillRect(QRect(area.left(), area.top(), area.width(), 2), theme::LineSoft);
        }
        painter.fillRect(QRect(area.right() - 1, area.top() + 8, 1, area.height() - 16), theme::LineSoft);

        const QColor iconColor = active ? theme::Accent : hovered ? theme::Fg : theme::Dim;
        const QPixmap pixmap = theme::pixelPixmap(m_glyph, iconColor, 1);
        const int contentWidth = pixmap.width() + 8 + QFontMetrics(font()).horizontalAdvance(text());
        int x = area.left() + (area.width() - contentWidth) / 2;
        painter.drawPixmap(x, area.center().y() - pixmap.height() / 2, pixmap);
        x += pixmap.width() + 8;

        painter.setFont(font());
        painter.setPen(active ? theme::Fg : hovered ? theme::Fg : theme::Dim);
        const QString shown = QFontMetrics(font()).elidedText(text(), Qt::ElideRight, area.right() - x - 6);
        painter.drawText(QRect(x, area.top(), area.right() - x - 6, area.height()), Qt::AlignVCenter | Qt::AlignLeft,
                         shown);
    }

   private:
    QStringList m_glyph;
};

// ------------------------------------------------------------------- helpers
void setGlyph(QPushButton* b, const QStringList& glyph, const QColor& color)
{
    b->setIcon(theme::pixelIcon(glyph, color));
    b->setIconSize(QSize(18, 18));
}

struct Tone {
    QColor fg;
    QColor bg;
    QColor border;
};

const Tone ToneOk{ theme::Ok, QColor(0x0F, 0x24, 0x1B), theme::OkDeep };
const Tone ToneWarn{ theme::Warn, QColor(0x27, 0x1F, 0x0E), theme::WarnDeep };
const Tone ToneAccent{ theme::AccentHi, QColor(0x28, 0x12, 0x1A), theme::AccentDeep };
const Tone ToneErr{ theme::Err, QColor(0x27, 0x12, 0x14), theme::ErrDeep };
const Tone ToneMuted{ theme::Muted, QColor(0x12, 0x16, 0x1D), theme::LineSoft };

void setChip(QLabel* label, const QString& text, const Tone& tone)
{
    label->setText(text);
    label->setWordWrap(false);
    label->setFont(theme::spaced(8, 0.4, true));
    label->setStyleSheet(QStringLiteral("color:%1; background:%2; border:2px solid %3; padding:2px 7px;")
                             .arg(tone.fg.name(), tone.bg.name(), tone.border.name()));
}

void setStatusLabel(QLabel* label, const QString& text, const QColor& color)
{
    label->setText(text);
    label->setStyleSheet(QStringLiteral("color:%1; background:#07090D; border:2px solid #222A37; "
                                        "border-left:4px solid %1; padding:7px 9px;")
                             .arg(color.name()));
}

PixelFrame* card(QWidget* parent, PixelFrame::Style style = PixelFrame::Card)
{
    return new PixelFrame(style, parent);
}

QLabel* caption(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("berryCaption"));
    label->setFont(theme::spaced(8, 0.4));
    label->setWordWrap(true);
    return label;
}

QLabel* sectionLabel(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("berryH2"));
    label->setFont(theme::spaced(10, 0.3, true));
    label->setWordWrap(true);
    return label;
}

QFrame* rule(QWidget* parent)
{
    auto* line = new QFrame(parent);
    line->setStyleSheet(QStringLiteral("background:#222A37; border:none;"));
    line->setFixedHeight(2);
    return line;
}

QWidget* accentRule(QWidget* parent)
{
    auto* wrap = new QWidget(parent);
    auto* row = new QHBoxLayout(wrap);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    auto* accent = new QFrame(wrap);
    accent->setStyleSheet(QStringLiteral("background:#FF4F79; border:none;"));
    accent->setFixedSize(56, 2);
    row->addWidget(accent);
    row->addWidget(rule(wrap), 1);
    return wrap;
}

PixelButton* button(const QString& text, QWidget* parent, PixelButton::Variant variant = PixelButton::Normal,
                    const QStringList& glyph = {})
{
    auto* b = new PixelButton(text, variant, glyph, parent);
    b->setFont(theme::regular(variant == PixelButton::Segment ? 11 : 13));
    return b;
}

QWidget* pageHeader(QWidget* parent, const QString& eyebrow, const QString& title, const QString& subtitle)
{
    auto* wrap = new QWidget(parent);
    auto* box = new QVBoxLayout(wrap);
    box->setContentsMargins(0, 0, 0, 2);
    box->setSpacing(4);

    auto* top = new QHBoxLayout;
    top->setSpacing(8);
    auto* mark = new QFrame(wrap);
    mark->setStyleSheet(QStringLiteral("background:#FF4F79; border:none;"));
    mark->setFixedSize(10, 10);
    top->addWidget(mark, 0, Qt::AlignVCenter);
    top->addWidget(caption(eyebrow, wrap));
    top->addStretch(1);
    box->addLayout(top);

    auto* heading = new QLabel(title, wrap);
    heading->setObjectName(QStringLiteral("berryTitle"));
    heading->setFont(theme::spaced(14, 0.4, true));
    heading->setWordWrap(true);
    box->addWidget(heading);

    if (!subtitle.isEmpty()) {
        auto* sub = new QLabel(subtitle, wrap);
        sub->setObjectName(QStringLiteral("berryMuted"));
        sub->setWordWrap(true);
        box->addWidget(sub);
    }

    box->addSpacing(8);
    box->addWidget(accentRule(wrap));
    return wrap;
}

int totalRamGb()
{
#ifdef Q_OS_WIN
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status))
        return static_cast<int>(status.ullTotalPhys / (1024ull * 1024ull * 1024ull));
#endif
    return 8;
}

QString humanGb(qint64 bytes)
{
    return QString::number(bytes / (1024ll * 1024ll * 1024ll));
}

}  // namespace

QStringList navGlyph(int index)
{
    switch (index) {
        case 0:
            return gHome();
        case 1:
            return gAccount();
        case 2:
            return gSkin();
        case 3:
            return gSettings();
        case 4:
            return gMonitor();
        default:
            return gInfo();
    }
}

Panel::Panel(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("berryRoot"));
    setAutoFillBackground(true);
    setAttribute(Qt::WA_StyledBackground, true);
    QPalette opaque = palette();
    opaque.setColor(QPalette::Window, QColor(0x0B, 0x0D, 0x12));
    setPalette(opaque);
    theme::apply(this);

    m_auth = new TelegramAuth(this);
    m_ping = new ServerPing(this);
    m_updater = new Updater(this);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ------------------------------------------------------------------ top bar
    auto* top = new QFrame(this);
    top->setObjectName(QStringLiteral("berryTopbar"));
    top->setFixedHeight(78);
    auto* topRow = new QHBoxLayout(top);
    topRow->setContentsMargins(18, 12, 18, 12);
    topRow->setSpacing(14);

    auto* logo = new QLabel(top);
    logo->setPixmap(QPixmap(QStringLiteral(":/berry/app-icon.png")).scaled(40, 40, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    logo->setFixedSize(40, 40);
    logo->setScaledContents(true);
    topRow->addWidget(logo, 0, Qt::AlignVCenter);

    auto* brand = new QVBoxLayout;
    brand->setSpacing(2);
    auto* wordmark = new QLabel(QStringLiteral("BERRYTRIP"), top);
    wordmark->setFont(theme::spaced(13, 0.5, true));
    wordmark->setStyleSheet(QStringLiteral("color:#F7FAFF;"));
    auto* tagline = caption(QStringLiteral("MINECRAFT 1.21.1 · NEOFORGE 21.1.267"), top);
    brand->addWidget(wordmark);
    brand->addWidget(tagline);
    topRow->addLayout(brand);
    topRow->addStretch(1);

    auto* versionChip = new QLabel(top);
    setChip(versionChip, QStringLiteral("V%1").arg(Config::version()), ToneMuted);
    topRow->addWidget(versionChip, 0, Qt::AlignVCenter);

    auto* licenseChip = new QLabel(top);
    setChip(licenseChip, QStringLiteral("GPL-3.0"), ToneAccent);
    topRow->addWidget(licenseChip, 0, Qt::AlignVCenter);
    // custom window frame: pixel min / max / close (window is frameless)
    auto* closeBtn = button(QStringLiteral("X"), top, PixelButton::Ghost, {});
    closeBtn->setFixedSize(36, 32);
    closeBtn->setToolTip(tr("Свернуть в трей"));
    connect(closeBtn, &QPushButton::clicked, this, [this] { window()->hide(); });
    auto* maxBtn = button(QStringLiteral("+"), top, PixelButton::Ghost, {});
    maxBtn->setFixedSize(36, 32);
    maxBtn->setToolTip(tr("Развернуть / свернуть"));
    connect(maxBtn, &QPushButton::clicked, this, [this] {
        auto* w = window();
        if (w->isMaximized()) w->showNormal(); else w->showMaximized();
    });
    auto* minBtn = button(QStringLiteral("-"), top, PixelButton::Ghost, {});
    minBtn->setFixedSize(36, 32);
    minBtn->setToolTip(tr("Свернуть"));
    connect(minBtn, &QPushButton::clicked, this, [this] { window()->showMinimized(); });
    topRow->addWidget(minBtn, 0, Qt::AlignVCenter);
    topRow->addWidget(maxBtn, 0, Qt::AlignVCenter);
    topRow->addWidget(closeBtn, 0, Qt::AlignVCenter);
    root->addWidget(top);
    top->installEventFilter(new TitleDragFilter(this));

    // -------------------------------------------------------------------- tabs
    auto* tabs = new QWidget(this);
    tabs->setObjectName(QStringLiteral("berryTabStrip"));
    tabs->setAttribute(Qt::WA_StyledBackground, true);
    auto* tabRow = new QHBoxLayout(tabs);
    tabRow->setContentsMargins(12, 0, 12, 0);
    tabRow->setSpacing(0);

    m_pages = new QStackedWidget(this);
    m_pages->setObjectName(QStringLiteral("berryPages"));
    m_pages->setAutoFillBackground(false);

    const QStringList titles{ tr("ГЛАВНОЕ"), tr("АККАУНТ"), tr("СКИНЫ"),
                              tr("НАСТРОЙКИ"), tr("ДИАГНОСТИКА"), tr("О ПРОГРАММЕ") };
    m_nav = new QButtonGroup(this);
    m_nav->setExclusive(true);
    for (int i = 0; i < titles.size(); ++i) {
        auto* tab = new PixelTab(titles.at(i), navGlyph(i), tabs);
        tab->setChecked(i == 0);
        m_nav->addButton(tab, i);
        tabRow->addWidget(tab);
    }
    tabRow->addStretch(1);
    root->addWidget(tabs);

    // ------------------------------------------------------------------- pages
    m_pages->addWidget(buildHome());
    m_pages->addWidget(buildAccount());
    m_pages->addWidget(buildSkins());
    m_pages->addWidget(buildSettings());
    m_pages->addWidget(buildDiagnostics());
    m_pages->addWidget(buildAbout());
    m_pages->setCurrentIndex(0);

    connect(m_nav, &QButtonGroup::idClicked, this, &Panel::goToPage);

    root->addWidget(m_pages, 1);

    // ------------------------------------------------------------------ footer
    auto* footer = new QFrame(this);
    footer->setObjectName(QStringLiteral("berryFooter"));
    footer->setFixedHeight(34);
    auto* footerRow = new QHBoxLayout(footer);
    footerRow->setContentsMargins(14, 0, 14, 0);
    footerRow->setSpacing(0);
    auto* footLeft = new QLabel(tr("СЕРВЕР %1").arg(Config::serverAddress()), footer);
    footLeft->setObjectName(QStringLiteral("berryFooterCell"));
    footLeft->setWordWrap(true);
    auto* footMid = new QLabel(QStringLiteral("TELEGRAM · @KILLBERRIESBOT"), footer);
    footMid->setObjectName(QStringLiteral("berryFooterCell"));
    auto* footRight = new QLabel(QStringLiteral("BERRYTRIP %1 · PIXEL UI").arg(Config::version()), footer);
    footRight->setObjectName(QStringLiteral("berryFooterCell"));
    footRight->setWordWrap(true);
    footerRow->addWidget(footLeft);
    footerRow->addStretch(1);
    footerRow->addWidget(footMid);
    footerRow->addStretch(1);
    footerRow->addWidget(footRight);
    root->addWidget(footer);

    // ---------------------------------------------------------------- state
    QString error;
    m_instance = InstanceSetup::ensure(&error);
    if (!m_instance) {
        m_status->setText(error.isEmpty() ? tr("Не удалось подготовить сборку") : error);
        m_play->setEnabled(false);
        return;
    }

    m_loading = true;
    m_memory->setValue(qBound(2, m_instance->settings()->get(QStringLiteral("MaxMemAlloc")).toInt() / 1024, 32));
    if (m_instance->settings()->get(QStringLiteral("OverrideJavaLocation")).toBool())
        m_javaPath->setText(QDir::toNativeSeparators(m_instance->settings()->get(QStringLiteral("JavaPath")).toString()));
    m_nick->setText(m_auth->nick().isEmpty() ? QString() : m_auth->nick());

    // Always keep an offline account selected, so Minecraft never asks for a
    // Microsoft login. The saved Telegram nick is used when it is known.
    QString accountNick = m_auth->tokenNick();
    if (accountNick.isEmpty())
        accountNick = m_auth->nick();
    if (accountNick.isEmpty())
        accountNick = QStringLiteral("Player");
    TelegramAuth::useOfflineAccount(accountNick);
    for (int i = 0; i < Tweaks::mods().size(); ++i)
        m_modChecks.at(i)->setChecked(Tweaks::modEnabled(Tweaks::mods().at(i)));
    m_packs->setChecked(Tweaks::packsEnabled());
    m_shaders->setChecked(Tweaks::shadersEnabled());
    m_loading = false;

    connect(m_instance, &BaseInstance::runningStatusChanged, this, [this] {
        refreshLaunch();
        if (!m_instance->isRunning()) {
            // The game holds mod jars and configs open; the update could not
            // write them while it was running.
            if (!m_syncFinished)
                startSync();
            tryAutoInstall();
        }
    });

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(2000);
    connect(m_pollTimer, &QTimer::timeout, m_auth, &TelegramAuth::poll);

    connect(m_auth, &TelegramAuth::message, this, [this](const QString& text, bool failure) {
        m_authStatus->setText(text);
        m_authStatus->setStyleSheet(failure ? QStringLiteral("color: #FF6E7B;") : QStringLiteral("color: #7C8798;"));
        refreshAuth();
    });
    connect(m_auth, &TelegramAuth::authenticated, this, [this](const QString& nick) {
        TelegramAuth::useOfflineAccount(nick);
        refreshAuth();
    });

    connect(m_ping, &ServerPing::finished, this, [this](const ServerPing::Status& status) {
        if (status.ok) {
            m_motd->setText(status.motd.isEmpty() ? tr("Без описания") : status.motd);
            m_players->setText(status.max > 0 ? QStringLiteral("Игроки: %1/%2").arg(status.online).arg(status.max)
                                              : tr("Игроки: %1").arg(status.online));
            m_pingLabel->setText(QStringLiteral("PING %1 MS").arg(status.ping));
            m_pingLabel->setStyleSheet(status.ping < 120 ? QStringLiteral("color:#4BE39F;")
                                                         : status.ping < 300 ? QStringLiteral("color:#F5B942;")
                                                                             : QStringLiteral("color:#FF6E7B;"));
        } else {
            m_motd->setText(tr("Сервер недоступен"));
            m_players->setText(status.error);
            m_pingLabel->setText(QStringLiteral("PING —"));
            m_pingLabel->setStyleSheet(QStringLiteral("color:#FF6E7B;"));
        }
    });

    Tweaks::applyAll();

    auto* dots = new QTimer(this);
    dots->setInterval(320);
    connect(dots, &QTimer::timeout, this, [this] {
        if (m_sync && m_sync->isRunning() && !m_statusBase.isEmpty()) {
            static const QStringList frames{ QStringLiteral(""), QStringLiteral(" ·"), QStringLiteral(" ··"),
                                             QStringLiteral(" ···") };
            m_status->setText(m_statusBase + frames.at(m_busyFrame % frames.size()));
            ++m_busyFrame;
        }
    });
    dots->start();

    refreshAuth();
    refreshLaunch();
    refreshSkins();
    refreshDiagnostics();
    startSync();
    startPing();

    m_pingTimer = new QTimer(this);
    m_pingTimer->setInterval(3000);
    connect(m_pingTimer, &QTimer::timeout, this, &Panel::startPing);
    m_pingTimer->start();

    connect(m_updater, &Updater::checking, this, [this] {
        if (m_aboutStatus)
            m_aboutStatus->setText(tr("Проверяю GitHub…"));
    });
    connect(m_updater, &Updater::upToDate, this, [this](const QString& version) {
        if (m_updateBanner)
            m_updateBanner->hide();
        if (m_aboutUpdate)
            m_aboutUpdate->hide();
        m_updateReady = false;
        m_autoInstallPending = false;
        if (m_aboutStatus) {
            m_aboutStatus->setText(tr("Установлена последняя версия: %1").arg(version));
            m_aboutStatus->setStyleSheet(QStringLiteral("color:#4BE39F;"));
        }
    });
    connect(m_updater, &Updater::available, this,
            [this](const QString& version, const QString&, const QUrl& releasePage, const QUrl& installerUrl,
                   const QString&, qint64) {
                m_updateVersion = version;
                m_updatePageUrl = releasePage.toString();
                m_updateCanAuto = !installerUrl.isEmpty();
                m_updateReady = false;
                m_autoInstallPending = m_updateCanAuto;
                if (m_updateBanner) {
                    m_updateBanner->show();
                    m_updateText->setText(tr("Доступно обновление BerryTrip %1.").arg(version));
                    m_updateButton->setText(m_updateCanAuto ? tr("Скачать") : tr("Открыть релиз"));
                    m_updateButton->setEnabled(true);
                    m_updateProgress->hide();
                }
                if (m_aboutUpdate) {
                    m_aboutUpdate->setText(m_updateCanAuto ? tr("Скачать обновление") : tr("Открыть релиз"));
                    m_aboutUpdate->setEnabled(true);
                    m_aboutUpdate->show();
                }
                if (m_aboutStatus) {
                    m_aboutStatus->setText(tr("Доступна версия %1 — скачиваю автоматически.").arg(version));
                    m_aboutStatus->setStyleSheet(QStringLiteral("color:#FF5D86;"));
                }
                if (m_updateCanAuto)
                    m_updater->download();
            });
    connect(m_updater, &Updater::checkFailed, this, [this](const QString& error) {
        m_autoInstallPending = false;
        if (m_updateBanner)
            m_updateBanner->hide();
        if (m_aboutUpdate)
            m_aboutUpdate->hide();
        if (m_aboutStatus) {
            m_aboutStatus->setText(tr("Не удалось проверить обновление: %1").arg(error));
            m_aboutStatus->setStyleSheet(QStringLiteral("color:#F5B942;"));
        }
    });
    connect(m_updater, &Updater::downloading, this, [this](const QString& version) {
        if (m_updateBanner) {
            m_updateBanner->show();
            m_updateText->setText(tr("Скачиваю BerryTrip %1…").arg(version));
            m_updateButton->setText(tr("Скачивание…"));
            m_updateButton->setEnabled(false);
            m_updateProgress->setRange(0, 0);
            m_updateProgress->show();
        }
        if (m_aboutStatus)
            m_aboutStatus->setText(tr("Скачиваю обновление %1…").arg(version));
    });
    connect(m_updater, &Updater::downloadProgress, this, [this](qint64 received, qint64 total) {
        const int percent = total > 0 ? static_cast<int>((received * 100) / total) : 0;
        if (m_updateProgress && total > 0) {
            m_updateProgress->setRange(0, 100);
            m_updateProgress->setValue(percent);
        }
        if (m_updateText && total > 0)
            m_updateText->setText(tr("Скачиваю обновление: %1%").arg(percent));
        if (m_aboutStatus && total > 0)
            m_aboutStatus->setText(tr("Скачиваю обновление: %1%").arg(percent));
    });
    connect(m_updater, &Updater::readyToInstall, this, [this](const QString&) {
        m_updateReady = true;
        m_updateCanAuto = true;
        m_autoInstallPending = true;
        if (m_updateBanner) {
            m_updateBanner->show();
            m_updateText->setText(tr("Обновление %1 загружено и проверено. Установлю автоматически…").arg(m_updateVersion));
            m_updateButton->setText(tr("Установить"));
            m_updateButton->setEnabled(true);
            m_updateProgress->hide();
        }
        if (m_aboutUpdate) {
            m_aboutUpdate->setText(tr("Перезапустить и установить"));
            m_aboutUpdate->setEnabled(true);
            m_aboutUpdate->show();
        }
        if (m_aboutStatus)
            m_aboutStatus->setText(tr("Обновление %1 скачано — установлю автоматически.").arg(m_updateVersion));
        QTimer::singleShot(1500, this, &Panel::tryAutoInstall);
    });
    connect(m_updater, &Updater::failed, this, [this](const QString& error) {
        m_autoInstallPending = false;
        if (m_updateBanner) {
            m_updateBanner->show();
            m_updateText->setText(error);
            m_updateButton->setText(m_updateCanAuto ? tr("Повторить") : tr("Открыть релиз"));
            m_updateButton->setEnabled(true);
            m_updateProgress->hide();
        }
        if (m_aboutStatus) {
            m_aboutStatus->setText(error);
            m_aboutStatus->setStyleSheet(QStringLiteral("color:#FF6E7B;"));
        }
    });

    QTimer::singleShot(6000, this, [this] { checkUpdates(); });
}

QWidget* Panel::page(QWidget* content)
{
    content->setObjectName(QStringLiteral("berryPage"));
    content->setAttribute(Qt::WA_StyledBackground, true);
    content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto* center = new QWidget;
    center->setObjectName(QStringLiteral("berryPage"));
    center->setAttribute(Qt::WA_StyledBackground, true);
    auto* centerBox = new QHBoxLayout(center);
    centerBox->setContentsMargins(0, 0, 0, 0);
    centerBox->setSpacing(0);
    centerBox->addWidget(content, 1);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(center);
    return scroll;
}

void Panel::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
    dither(painter, rect(), theme::Bg, theme::BgDot, 4);

    // A faint 16px grid keeps every block on the same pixel rhythm.
    painter.setPen(theme::mix(theme::Bg, QColor(Qt::white), 4));
    for (int x = 0; x < width(); x += 16)
        painter.drawLine(x, 0, x, height());
    for (int y = 0; y < height(); y += 16)
        painter.drawLine(0, y, width(), y);
}

QWidget* Panel::buildHome()
{
    auto* content = new QWidget;
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(26, 18, 26, 18);
    layout->setSpacing(14);

    // ------------------------------------------------------- launcher update
    m_updateBanner = new PixelFrame(PixelFrame::Banner, content);
    auto* bannerLayout = new QVBoxLayout(m_updateBanner);
    bannerLayout->setContentsMargins(16, 12, 16, 14);
    bannerLayout->setSpacing(8);
    auto* bannerHead = new QHBoxLayout;
    bannerHead->setSpacing(10);
    bannerHead->addWidget(caption(tr("ОБНОВЛЕНИЕ ЛАУНЧЕРА"), m_updateBanner));
    bannerHead->addStretch(1);
    m_updateButton = button(tr("Скачать"), m_updateBanner, PixelButton::Primary, gDownload());
    connect(m_updateButton, &QPushButton::clicked, this, &Panel::installUpdate);
    bannerHead->addWidget(m_updateButton);
    bannerLayout->addLayout(bannerHead);
    m_updateText = new QLabel(tr("Доступна новая версия."), m_updateBanner);
    m_updateText->setObjectName(QStringLiteral("berryMuted"));
    m_updateText->setWordWrap(true);
    bannerLayout->addWidget(m_updateText);
    m_updateProgress = new PixelProgress(m_updateBanner);
    m_updateProgress->hide();
    bannerLayout->addWidget(m_updateProgress);
    m_updateBanner->hide();
    layout->addWidget(m_updateBanner);

    // ------------------------------------------------------------------- hero
    auto* hero = new PixelFrame(PixelFrame::Hero, content);
    auto* heroRow = new QHBoxLayout(hero);
    heroRow->setContentsMargins(18, 14, 18, 16);
    heroRow->setSpacing(16);

    auto* left = new QVBoxLayout;
    left->setSpacing(6);

    auto* titleRow = new QHBoxLayout;
    titleRow->setSpacing(14);
    auto* heroLogo = new QLabel(hero);
    heroLogo->setPixmap(QPixmap(QStringLiteral(":/berry/app-icon.png")).scaled(56, 56, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    heroLogo->setFixedSize(56, 56);
    heroLogo->setScaledContents(true);
    titleRow->addWidget(heroLogo, 0, Qt::AlignTop);

    auto* titleBox = new QVBoxLayout;
    titleBox->setSpacing(2);
    auto* title = new QLabel(QStringLiteral("BERRYTRIP"), hero);
    title->setObjectName(QStringLiteral("berryDisplay"));
    title->setFont(theme::spaced(16, 0.5, true));
    title->setWordWrap(true);
    titleBox->addWidget(title);
    auto* subtitle = new QLabel(QStringLiteral("MINECRAFT 1.21.1 · NEOFORGE 21.1.267"), hero);
    subtitle->setObjectName(QStringLiteral("berryCaption"));
    titleBox->addWidget(subtitle);
    titleRow->addLayout(titleBox, 1);

    m_chip = new QLabel(hero);
    setChip(m_chip, tr("ПРОВЕРКА СБОРКИ"), ToneMuted);
    titleBox->addSpacing(6);
    titleBox->addWidget(m_chip, 0, Qt::AlignLeft);
    left->addLayout(titleRow);

    left->addSpacing(8);
    m_play = button(tr("Играть"), hero, PixelButton::Primary, gPlay());
    m_play->setFont(theme::spaced(12, 0.4, true));
    m_play->setMinimumSize(198, 52);
    m_play->setMaximumHeight(52);
    connect(m_play, &QPushButton::clicked, this, &Panel::onPlay);
    auto* playRow = new QHBoxLayout;
    playRow->setSpacing(10);
    playRow->addWidget(m_play);
    auto* playHint = new QLabel(tr("Запуск на сервер\nбез входа в Microsoft"), hero);
    playHint->setObjectName(QStringLiteral("berryDim"));
    playRow->addWidget(playHint);
    playRow->addStretch(1);
    left->addLayout(playRow);

    auto* marks = new QHBoxLayout;
    marks->setSpacing(6);
    auto* modChip = new QLabel(hero);
    setChip(modChip, tr("МОДОВ %1").arg(Tweaks::modCount()), ToneMuted);
    auto* mcChip = new QLabel(hero);
    setChip(mcChip, QStringLiteral("1.21.1"), ToneMuted);
    auto* loaderChip = new QLabel(hero);
    setChip(loaderChip, QStringLiteral("NEOFORGE 21.1.267"), ToneMuted);
    marks->addWidget(modChip);
    marks->addWidget(mcChip);
    marks->addWidget(loaderChip);
    marks->addStretch(1);
    left->addSpacing(4);
    left->addLayout(marks);

    auto* features = new QVBoxLayout;
    features->setSpacing(4);
    const auto feature = [&](const QStringList& glyph, const QString& text) {
        auto* row = new QHBoxLayout;
        row->setSpacing(8);
        auto* icon = new QLabel(hero);
        icon->setPixmap(theme::pixelPixmap(glyph, theme::AccentHi));
        icon->setFixedSize(18, 18);
        auto* label = new QLabel(text, hero);
        label->setObjectName(QStringLiteral("berryDim"));
        row->addWidget(icon, 0, Qt::AlignVCenter);
        row->addWidget(label);
        row->addStretch(1);
        features->addLayout(row);
    };
    feature(gRefresh(), tr("Сборка скачивается и обновляется сама"));
    feature(gShield(), tr("Вход без Microsoft и без лицензии"));
    feature(gServer(), tr("Сервер %1").arg(Config::serverAddress()));
    left->addSpacing(8);
    left->addLayout(features);
    left->addStretch(1);
    heroRow->addLayout(left, 1);

    auto* showcase = new PixelFrame(PixelFrame::Inset, hero);
    showcase->setFixedWidth(262);
    auto* showcaseLayout = new QVBoxLayout(showcase);
    showcaseLayout->setContentsMargins(10, 8, 10, 10);
    showcaseLayout->setSpacing(6);
    auto* showcaseHead = new QHBoxLayout;
    showcaseHead->setSpacing(8);
    showcaseHead->addWidget(caption(tr("ТВОЙ СКИН"), showcase));
    showcaseHead->addStretch(1);
    auto* viewTag = new QLabel(QStringLiteral("3D"), showcase);
    viewTag->setObjectName(QStringLiteral("berryDim"));
    showcaseHead->addWidget(viewTag);
    showcaseLayout->addLayout(showcaseHead);

    m_skinPreview = new Skin3D(showcase);
    m_skinPreview->setMinimumSize(230, 172);
    m_skinPreview->setMaximumHeight(196);
    m_skinPreview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    showcaseLayout->addWidget(m_skinPreview, 1);

    m_skinName = new QLabel(tr("Скин не выбран"), showcase);
    m_skinName->setObjectName(QStringLiteral("berryDim"));
    m_skinName->setWordWrap(true);
    showcaseLayout->addWidget(m_skinName);

    auto* openSkins = button(tr("Открыть скины"), showcase, PixelButton::Ghost, gSkin());
    connect(openSkins, &QPushButton::clicked, this, [this] { goToPage(2); });
    showcaseLayout->addWidget(openSkins);
    heroRow->addWidget(showcase, 0);
    layout->addWidget(hero);

    // ---------------------------------------------------------------- 3 cards
    auto* row = new QHBoxLayout;
    row->setSpacing(14);

    auto* server = card(content);
    auto* serverLayout = new QVBoxLayout(server);
    serverLayout->setContentsMargins(16, 12, 16, 14);
    serverLayout->setSpacing(6);
    serverLayout->addWidget(caption(tr("СЕРВЕР"), server));
    auto* address = new QLabel(Config::serverAddress(), server);
    address->setObjectName(QStringLiteral("berryMono"));
    serverLayout->addWidget(address);
    m_motd = new QLabel(tr("Проверяю сервер…"), server);
    m_motd->setObjectName(QStringLiteral("berryH1"));
    m_motd->setWordWrap(true);
    serverLayout->addWidget(m_motd);
    auto* serverState = new QHBoxLayout;
    serverState->setSpacing(12);
    m_players = new QLabel(tr("Игроки: —"), server);
    m_players->setObjectName(QStringLiteral("berryMuted"));
    m_pingLabel = new QLabel(tr("PING —"), server);
    m_pingLabel->setObjectName(QStringLiteral("berryDim"));
    serverState->addWidget(m_players);
    serverState->addWidget(m_pingLabel);
    serverState->addStretch(1);
    serverLayout->addLayout(serverState);
    serverLayout->addStretch(1);
    auto* serverButtons = new QHBoxLayout;
    serverButtons->setSpacing(8);
    // No Ping button: the status refreshes automatically every 3 seconds.
    auto* telegram = button(tr("Telegram"), server, PixelButton::Normal, gTelegram());
    connect(telegram, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl(TelegramUrl)); });
    serverButtons->addWidget(telegram);
    serverButtons->addStretch(1);
    serverLayout->addLayout(serverButtons);
    row->addWidget(server, 1);

    auto* account = card(content);
    auto* accountLayout = new QVBoxLayout(account);
    accountLayout->setContentsMargins(16, 12, 16, 14);
    accountLayout->setSpacing(6);
    accountLayout->addWidget(caption(tr("АККАУНТ"), account));
    m_homeAccount = new QLabel(tr("Вход не выполнен"), account);
    m_homeAccount->setObjectName(QStringLiteral("berryH1"));
    m_homeAccount->setWordWrap(true);
    accountLayout->addWidget(m_homeAccount);
    auto* accountNote = new QLabel(QStringLiteral("TELEGRAM · @KILLBERRIESBOT"), account);
    accountNote->setObjectName(QStringLiteral("berryCaption"));
    accountLayout->addWidget(accountNote);
    accountLayout->addStretch(1);
    auto* openAccount = button(tr("Аккаунт"), account, PixelButton::Normal, gAccount());
    connect(openAccount, &QPushButton::clicked, this, [this] { goToPage(1); });
    accountLayout->addWidget(openAccount, 0, Qt::AlignLeft);
    row->addWidget(account, 1);

    auto* build = card(content);
    auto* buildLayout = new QVBoxLayout(build);
    buildLayout->setContentsMargins(16, 12, 16, 14);
    buildLayout->setSpacing(8);
    buildLayout->addWidget(caption(tr("СБОРКА"), build));
    m_progress = new PixelProgress(build);
    buildLayout->addWidget(m_progress);
    m_status = new QLabel(tr("Подготовка…"), build);
    m_status->setObjectName(QStringLiteral("berryDim"));
    m_status->setWordWrap(true);
    buildLayout->addWidget(m_status);
    buildLayout->addStretch(1);
    auto* buildActions = new QHBoxLayout;
    buildActions->setSpacing(8);
    m_retry = button(tr("Повторить"), build, PixelButton::Normal, gRefresh());
    m_retry->hide();
    connect(m_retry, &QPushButton::clicked, this, &Panel::startSync);
    auto* site = button(tr("Сайт"), build, PixelButton::Ghost, gGlobe());
    connect(site, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl(SiteUrl)); });
    auto* releases = button(tr("Релизы"), build, PixelButton::Ghost, gDownload());
    connect(releases, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl(ReleasesUrl)); });
    buildActions->addWidget(m_retry);
    buildActions->addStretch(1);
    buildActions->addWidget(site);
    buildActions->addWidget(releases);
    buildLayout->addLayout(buildActions);
    row->addWidget(build, 1);

    layout->addLayout(row);
    layout->addStretch(1);

    return page(content);
}

QWidget* Panel::buildAccount()
{
    auto* content = new QWidget;
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(26, 20, 26, 20);
    layout->setSpacing(16);

    layout->addWidget(pageHeader(content, tr("АККАУНТ"), tr("Вход"),
                                 tr("Только через Telegram-бота. Microsoft-аккаунт и лицензия не нужны.")));

    auto* body = new QHBoxLayout;
    body->setSpacing(16);

    auto* left = new QVBoxLayout;
    left->setSpacing(14);

    auto* loginCard = card(content);
    auto* loginLayout = new QVBoxLayout(loginCard);
    loginLayout->setContentsMargins(18, 14, 18, 16);
    loginLayout->setSpacing(8);
    loginLayout->addWidget(caption(tr("ВХОД ЧЕРЕЗ TELEGRAM"), loginCard));

    auto* nickCaption = new QLabel(tr("Ник в Minecraft"), loginCard);
    nickCaption->setObjectName(QStringLiteral("berryDim"));
    loginLayout->addWidget(nickCaption);

    auto* nickRow = new QHBoxLayout;
    nickRow->setSpacing(8);
    m_nick = new QLineEdit(loginCard);
    m_nick->setPlaceholderText(tr("3–16 символов: латиница, цифры, _"));
    m_nick->setMinimumHeight(38);
    m_nick->setFont(theme::regular(13));
    nickRow->addWidget(m_nick, 1);
    m_login = button(tr("Войти через Telegram"), loginCard, PixelButton::Primary, gTelegram());
    m_login->setMinimumHeight(38);
    connect(m_login, &QPushButton::clicked, this, &Panel::onAuthClicked);
    nickRow->addWidget(m_login);
    loginLayout->addLayout(nickRow);

    m_authStatus = new QLabel(loginCard);
    m_authStatus->setWordWrap(true);
    setStatusLabel(m_authStatus, tr("Введи ник и подтверди вход в боте."), theme::Dim);
    loginLayout->addWidget(m_authStatus);

    loginLayout->addWidget(rule(loginCard));

    auto* secondary = new QHBoxLayout;
    secondary->setSpacing(8);
    m_openTelegram = button(tr("Скопировать ссылку"), loginCard, PixelButton::Ghost, gCopy());
    connect(m_openTelegram, &QPushButton::clicked, this, [this] {
        const QString link = m_auth->pendingLink();
        if (link.isEmpty()) {
            setStatusLabel(m_authStatus, tr("Ссылка ещё не получена — начни вход заново."), theme::Warn);
            return;
        }
        QApplication::clipboard()->setText(link);
        setStatusLabel(m_authStatus, tr("Ссылка скопирована — открой её в Telegram или браузере."), theme::Ok);
    });
    m_openTelegram->hide();
    m_logout = button(tr("Выйти"), loginCard, PixelButton::Ghost, gLogout());
    connect(m_logout, &QPushButton::clicked, this, [this] {
        m_auth->logout();
        refreshAuth();
    });
    secondary->addWidget(m_openTelegram);
    secondary->addWidget(m_logout);
    secondary->addStretch(1);
    loginLayout->addLayout(secondary);
    left->addWidget(loginCard);

    auto* steps = card(content);
    auto* stepsLayout = new QVBoxLayout(steps);
    stepsLayout->setContentsMargins(18, 14, 18, 16);
    stepsLayout->setSpacing(10);
    stepsLayout->addWidget(caption(tr("КАК ЭТО РАБОТАЕТ"), steps));

    const QVector<QPair<QString, QString>> rows{
        { QStringLiteral("1"), tr("Введи ник, под которым играешь.") },
        { QStringLiteral("2"), tr("Лаунчер откроет Telegram Desktop или браузер.") },
        { QStringLiteral("3"), tr("Нажми Start в Telegram — ник попадёт в вайтлист.") },
        { QStringLiteral("4"), tr("Токен сохранится, кнопка «Играть» разблокируется.") },
    };
    for (const auto& row : rows) {
        auto* line = new QHBoxLayout;
        line->setSpacing(10);
        auto* number = new QLabel(row.first, steps);
        number->setAlignment(Qt::AlignCenter);
        number->setFixedSize(24, 24);
        number->setFont(theme::spaced(10, 0, true));
        number->setStyleSheet(QStringLiteral("color:#FF9DB4; background:#241018; border:2px solid #6E1430;"));
        auto* text = new QLabel(row.second, steps);
        text->setObjectName(QStringLiteral("berryMuted"));
        text->setWordWrap(true);
        line->addWidget(number, 0, Qt::AlignTop);
        line->addWidget(text, 1);
        stepsLayout->addLayout(line);
    }
    left->addWidget(steps);
    left->addStretch(1);
    body->addLayout(left, 1);

    auto* tips = card(content);
    tips->setFixedWidth(320);
    auto* tipsLayout = new QVBoxLayout(tips);
    tipsLayout->setContentsMargins(16, 14, 16, 16);
    tipsLayout->setSpacing(10);
    tipsLayout->addWidget(caption(tr("ПОДСКАЗКИ"), tips));

    const auto addTip = [&](const QStringList& glyph, const QString& text) {
        auto* row = new QHBoxLayout;
        row->setSpacing(10);
        auto* icon = new QLabel(tips);
        icon->setPixmap(theme::pixelPixmap(glyph, theme::Accent));
        icon->setFixedSize(18, 18);
        auto* label = new QLabel(text, tips);
        label->setObjectName(QStringLiteral("berryDim"));
        label->setWordWrap(true);
        row->addWidget(icon, 0, Qt::AlignTop);
        row->addWidget(label, 1);
        tipsLayout->addLayout(row);
    };
    addTip(gShield(), tr("Один ник — один человек. Передавать вход нельзя."));
    addTip(gKey(), tr("Токен входа живёт 30 дней, потом нужен новый Start."));
    addTip(gTelegram(), tr("Всё подтверждение — одна кнопка Start в боте."));
    addTip(gClock(), tr("Первый вход после вайтлиста может занять минуту."));

    tipsLayout->addStretch(1);
    tipsLayout->addWidget(rule(tips));
    tipsLayout->addSpacing(2);
    auto* bot = new QLabel(QStringLiteral("@killberriesbot"), tips);
    bot->setObjectName(QStringLiteral("berryMono"));
    tipsLayout->addWidget(bot);
    auto* openBot = button(tr("Открыть бота"), tips, PixelButton::Normal, gTelegram());
    connect(openBot, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl(TelegramUrl)); });
    tipsLayout->addWidget(openBot);

    body->addWidget(tips, 0, Qt::AlignTop);
    layout->addLayout(body, 1);
    layout->addStretch(1);

    return page(content);
}

QWidget* Panel::buildSkins()
{
    auto* content = new QWidget;
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(26, 18, 26, 18);
    layout->setSpacing(12);

    layout->addWidget(pageHeader(content, tr("QUICK SKIN"), tr("Скины"),
                                 tr("Скин ставится прямо в библиотеку Quick Skin — в игре он появится сразу.")));

    auto* modelCard = card(content);
    auto* modelLayout = new QVBoxLayout(modelCard);
    modelLayout->setContentsMargins(16, 10, 16, 12);
    modelLayout->setSpacing(6);
    auto* modelHead = new QHBoxLayout;
    modelHead->setSpacing(10);
    modelHead->addWidget(caption(tr("МОДЕЛЬ"), modelCard));
    modelHead->addStretch(1);
    auto* modelHint = new QLabel(tr("3D-модель видна здесь и на главном экране."), modelCard);
    modelHint->setObjectName(QStringLiteral("berryDim"));
    modelHead->addWidget(modelHint);
    modelLayout->addLayout(modelHead);

    auto* models = new QHBoxLayout;
    models->setSpacing(6);
    auto* modelGroup = new QButtonGroup(modelCard);
    modelGroup->setExclusive(true);
    const auto addModel = [&](const QString& text, const QString& value) {
        auto* b = button(text, modelCard, PixelButton::Segment);
        b->setCheckable(true);
        modelGroup->addButton(b);
        connect(b, &QPushButton::clicked, this, [this, value] {
            Skins::setModel(value);
            refreshSkins();
        });
        models->addWidget(b);
        return b;
    };
    m_modelAuto = addModel(QStringLiteral("AUTO"), QStringLiteral("auto"));
    m_modelClassic = addModel(QStringLiteral("CLASSIC"), QStringLiteral("classic"));
    m_modelSlim = addModel(QStringLiteral("SLIM"), QStringLiteral("slim"));
    models->addStretch(1);
    modelLayout->addLayout(models);
    layout->addWidget(modelCard);

    auto* columns = new QHBoxLayout;
    columns->setSpacing(14);

    // The 3D skin is the one allowed exception to the pixel grid.
    auto* previewCard = card(content);
    previewCard->setFixedWidth(272);
    previewCard->setFixedHeight(314);
    auto* previewLayout = new QVBoxLayout(previewCard);
    previewLayout->setContentsMargins(10, 8, 10, 10);
    previewLayout->setSpacing(4);
    previewLayout->addWidget(caption(tr("ПРЕДПРОСМОТР"), previewCard));
    m_skinPagePreview = new Skin3D(previewCard);
    m_skinPagePreview->setFixedHeight(262);
    previewLayout->addWidget(m_skinPagePreview);
    columns->addWidget(previewCard, 0);

    auto* skinCard = card(content);
    skinCard->setFixedHeight(314);
    auto* skinLayout = new QVBoxLayout(skinCard);
    skinLayout->setContentsMargins(16, 10, 16, 12);
    skinLayout->setSpacing(6);
    skinLayout->addWidget(caption(tr("СКИНЫ В БИБЛИОТЕКЕ"), skinCard));
    auto* skinButtons = new QHBoxLayout;
    skinButtons->setSpacing(8);
    auto* pickSkin = button(tr("Выбрать файл"), skinCard, PixelButton::Normal, gFolder());
    connect(pickSkin, &QPushButton::clicked, this, &Panel::pickSkin);
    auto* applySkin = button(tr("Применить"), skinCard, PixelButton::Primary, gCheck());
    connect(applySkin, &QPushButton::clicked, this, &Panel::applySelectedSkin);
    skinButtons->addWidget(pickSkin);
    skinButtons->addWidget(applySkin);
    skinButtons->addStretch(1);
    skinLayout->addLayout(skinButtons);
    m_skinList = new QListWidget(skinCard);
    m_skinList->setFixedHeight(198);
    m_skinList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_skinList->setFont(theme::regular(12));
    skinLayout->addWidget(m_skinList);
    skinLayout->addStretch(1);
    columns->addWidget(skinCard, 3);

    auto* capeCard = card(content);
    capeCard->setFixedHeight(314);
    auto* capeLayout = new QVBoxLayout(capeCard);
    capeLayout->setContentsMargins(16, 10, 16, 12);
    capeLayout->setSpacing(6);
    capeLayout->addWidget(caption(tr("ПЛАЩИ"), capeCard));
    auto* capeButtons = new QHBoxLayout;
    capeButtons->setSpacing(8);
    auto* pickCape = button(tr("Выбрать"), capeCard, PixelButton::Normal, gFolder());
    connect(pickCape, &QPushButton::clicked, this, &Panel::pickCape);
    auto* applyCape = button(tr("Включить"), capeCard, PixelButton::Primary, gCheck());
    connect(applyCape, &QPushButton::clicked, this, [this] {
        auto* item = m_capeList ? m_capeList->currentItem() : nullptr;
        if (!item) {
            setStatusLabel(m_skinStatus, tr("Выбери плащ в списке."), theme::Warn);
            return;
        }
        QString error;
        if (!Skins::activateCape(item->data(Qt::UserRole).toString(), &error)) {
            setStatusLabel(m_skinStatus, error, theme::Err);
            return;
        }
        setStatusLabel(m_skinStatus, tr("Плащ включён: %1").arg(item->text().mid(2)), theme::Ok);
        refreshSkins();
    });
    auto* clearCape = button(tr("Убрать"), capeCard, PixelButton::Ghost, gTrash());
    connect(clearCape, &QPushButton::clicked, this, [this] {
        Skins::clearCape();
        setStatusLabel(m_skinStatus, tr("Плащ выключен."), theme::Ok);
        refreshSkins();
    });
    capeButtons->addWidget(pickCape);
    capeButtons->addWidget(applyCape);
    capeButtons->addWidget(clearCape);
    capeButtons->addStretch(1);
    capeLayout->addLayout(capeButtons);
    m_capeName = new QLabel(tr("Плащ выключен"), capeCard);
    m_capeName->setObjectName(QStringLiteral("berryDim"));
    capeLayout->addWidget(m_capeName);
    m_capeList = new QListWidget(capeCard);
    m_capeList->setFixedHeight(150);
    m_capeList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_capeList->setFont(theme::regular(12));
    capeLayout->addWidget(m_capeList);
    capeLayout->addStretch(1);
    columns->addWidget(capeCard, 2);

    layout->addLayout(columns);

    m_skinStatus = new QLabel(content);
    m_skinStatus->setWordWrap(true);
    setStatusLabel(m_skinStatus, tr("Quick Skin проверяется…"), theme::Dim);
    layout->addWidget(m_skinStatus);
    layout->addStretch(1);

    connect(m_skinList, &QListWidget::itemSelectionChanged, this, [this] {
        if (auto* item = m_skinList->currentItem())
            refreshSkinPreview(item->data(Qt::UserRole).toString());
    });

    return page(content);
}

QString Panel::modelValue() const
{
    return Skins::model();
}

void Panel::refreshSkins()
{
    if (!m_skinList || !m_capeList)
        return;

    const QString available = Skins::availability();
    if (!available.isEmpty()) {
        m_skinStatus->setText(available);
        m_skinStatus->setStyleSheet(QStringLiteral("color:#F5B942;"));
    }

    const QString model = modelValue();
    m_modelAuto->setChecked(model == QLatin1String("auto"));
    m_modelClassic->setChecked(model == QLatin1String("classic"));
    m_modelSlim->setChecked(model == QLatin1String("slim"));

    m_skinList->clear();
    QListWidgetItem* activeSkinItem = nullptr;
    for (const auto& entry : Skins::entries(false)) {
        auto* item = new QListWidgetItem((entry.active ? QStringLiteral("● ") : QStringLiteral("   ")) + entry.name, m_skinList);
        item->setData(Qt::UserRole, entry.path);
        item->setToolTip(entry.size + QChar(10) + entry.path);
        if (entry.active)
            activeSkinItem = item;
    }
    if (activeSkinItem)
        m_skinList->setCurrentItem(activeSkinItem);

    m_capeList->clear();
    QListWidgetItem* activeCapeItem = nullptr;
    for (const auto& entry : Skins::entries(true)) {
        auto* item = new QListWidgetItem((entry.active ? QStringLiteral("● ") : QStringLiteral("   ")) + entry.name, m_capeList);
        item->setData(Qt::UserRole, entry.path);
        item->setToolTip(entry.size + QChar(10) + entry.path);
        if (entry.active)
            activeCapeItem = item;
    }
    if (activeCapeItem)
        m_capeList->setCurrentItem(activeCapeItem);

    const QString current = Skins::currentSkin();
    refreshSkinPreview(current);
    m_skinName->setText(current.isEmpty() ? tr("Скин не выбран") : tr("Активен: %1").arg(QFileInfo(current).fileName()));

    const QString cape = Skins::currentCape();
    m_capeName->setText(cape.isEmpty() ? tr("Плащ выключен") : tr("Активен: %1").arg(QFileInfo(cape).fileName()));

    if (available.isEmpty()) {
        m_skinStatus->setText(tr("Quick Skin готов: изменения видны игре без перезапуска лаунчера."));
        m_skinStatus->setStyleSheet(QStringLiteral("color:#4BE39F;"));
    }
}

void Panel::refreshSkinPreview(const QString& path)
{
    if (!m_skinPreview)
        return;

    QImageReader reader(path);
    const QImage image = reader.read();
    const bool slim = modelValue() == QLatin1String("slim") ||
                      (modelValue() == QLatin1String("auto") && Skins::detectSlim(image));
    m_skinPreview->setSlim(slim);
    m_skinPreview->setSkin(image);

    const QString capePath = Skins::currentCape();
    m_skinPreview->setCape(capePath.isEmpty() ? QImage() : QImage(capePath));
    if (m_skinPagePreview) {
        m_skinPagePreview->setSlim(slim);
        m_skinPagePreview->setSkin(image);
        m_skinPagePreview->setCape(capePath.isEmpty() ? QImage() : QImage(capePath));
    }
}

void Panel::pickSkin()
{
    const QString file = QFileDialog::getOpenFileName(this, tr("Скин для Quick Skin"), QString(),
                                                      tr("Изображения (*.png *.gif)"), nullptr, QFileDialog::DontUseNativeDialog);
    if (file.isEmpty())
        return;

    const QString nick = m_auth->tokenNick().isEmpty() ? m_auth->nick() : m_auth->tokenNick();
    QString error;
    if (!Skins::applySkin(file, nick, modelValue(), &error)) {
        m_skinStatus->setText(error);
        m_skinStatus->setStyleSheet(QStringLiteral("color:#FF6E7B;"));
        return;
    }
    m_pickedSkin = file;
    m_skinStatus->setText(tr("Скин установлен: %1").arg(QFileInfo(file).fileName()));
    m_skinStatus->setStyleSheet(QStringLiteral("color:#4BE39F;"));
    refreshSkins();
}

void Panel::applySelectedSkin()
{
    auto* item = m_skinList ? m_skinList->currentItem() : nullptr;
    if (!item) {
        m_skinStatus->setText(tr("Выбери скин в списке или нажми «Выбрать PNG/GIF»."));
        m_skinStatus->setStyleSheet(QStringLiteral("color:#F5B942;"));
        return;
    }

    const QString nick = m_auth->tokenNick().isEmpty() ? m_auth->nick() : m_auth->tokenNick();
    QString error;
    if (!Skins::activateSkin(item->data(Qt::UserRole).toString(), nick, modelValue(), &error)) {
        m_skinStatus->setText(error);
        m_skinStatus->setStyleSheet(QStringLiteral("color:#FF6E7B;"));
        return;
    }
    m_skinStatus->setText(tr("Скин включён: %1").arg(item->text().mid(2)));
    m_skinStatus->setStyleSheet(QStringLiteral("color:#4BE39F;"));
    refreshSkins();
}

void Panel::pickCape()
{
    const QString file = QFileDialog::getOpenFileName(this, tr("Плащ для Quick Skin"), QString(),
                                                      tr("Изображения (*.png *.gif)"), nullptr, QFileDialog::DontUseNativeDialog);
    if (file.isEmpty())
        return;
    QString error;
    if (!Skins::applyCape(file, &error)) {
        m_skinStatus->setText(error);
        m_skinStatus->setStyleSheet(QStringLiteral("color:#FF6E7B;"));
        return;
    }
    m_skinStatus->setText(tr("Плащ установлен: %1").arg(QFileInfo(file).fileName()));
    m_skinStatus->setStyleSheet(QStringLiteral("color:#4BE39F;"));
    refreshSkins();
}

void Panel::notice(const QString& text)
{
    if (text.isEmpty() || !m_status)
        return;
    m_statusBase = text;
    m_status->setText(text);
    m_status->setStyleSheet(QStringLiteral("color:#F5B942;"));
}

QWidget* Panel::buildSettings()
{
    auto* content = new QWidget;
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(26, 20, 26, 20);
    layout->setSpacing(14);

    layout->addWidget(pageHeader(content, tr("НАСТРОЙКИ"), tr("Сборка"),
                                 tr("Моды, графика, память и Java. Применяется сразу.")));

    auto* columns = new QHBoxLayout;
    columns->setSpacing(14);

    auto* left = new QVBoxLayout;
    left->setSpacing(14);

    auto* mods = card(content);
    auto* modsLayout = new QVBoxLayout(mods);
    modsLayout->setContentsMargins(18, 14, 18, 16);
    modsLayout->setSpacing(6);
    modsLayout->addWidget(caption(tr("МОДЫ"), mods));
    auto* modGrid = new QGridLayout;
    modGrid->setHorizontalSpacing(18);
    modGrid->setVerticalSpacing(2);
    int modIndex = 0;
    for (const Tweaks::Mod& mod : Tweaks::mods()) {
        auto* check = new PixelCheck(mod.label, mods);
        check->setToolTip(QStringLiteral("%1 — %2").arg(mod.label, mod.hint));
        m_modChecks.append(check);
        connect(check, &QCheckBox::toggled, this, [this, mod](bool on) {
            if (m_loading)
                return;
            Tweaks::setModEnabled(mod, on);
            if (on && mod.downloadable()) {
                m_settingsStatus->setText(tr("Скачиваю %1…").arg(mod.label));
                Tweaks::installOptionalMod(mod, [this, mod](bool ok, const QString& error) {
                    if (!ok) {
                        Tweaks::setModEnabled(mod, false);
                        for (int i = 0; i < Tweaks::mods().size(); ++i) {
                            if (Tweaks::mods().at(i).key != mod.key)
                                continue;
                            m_loading = true;
                            m_modChecks.at(i)->setChecked(false);
                            m_loading = false;
                        }
                        m_settingsStatus->setText(tr("%1: %2").arg(mod.label, error));
                    } else {
                        m_settingsStatus->setText(tr("%1 включён").arg(mod.label));
                    }
                    applyTweaks(mod.label);
                });
                return;
            }
            applyTweaks(mod.label);
        });
        modGrid->addWidget(check, modIndex / 2, modIndex % 2);
        ++modIndex;
    }
    modsLayout->addLayout(modGrid);
    modsLayout->addSpacing(2);
    auto* locked = new QLabel(tr("Всегда включены: JourneyMap · Emotecraft · Immersive Paintings · Not Enough Animations"),
                              mods);
    locked->setObjectName(QStringLiteral("berryDim"));
    locked->setWordWrap(true);
    modsLayout->addWidget(locked);
    left->addWidget(mods);

    auto* graphics = card(content);
    auto* graphicsLayout = new QVBoxLayout(graphics);
    graphicsLayout->setContentsMargins(18, 14, 18, 16);
    graphicsLayout->setSpacing(6);
    graphicsLayout->addWidget(caption(tr("ГРАФИКА"), graphics));
    m_packs = new PixelCheck(tr("Ресурспаки"), graphics);
    m_shaders = new PixelCheck(tr("Шейдеры"), graphics);
    connect(m_packs, &QCheckBox::toggled, this, [this](bool on) {
        if (m_loading)
            return;
        Tweaks::setPacksEnabled(on);
        applyTweaks(tr("Ресурспаки"));
    });
    connect(m_shaders, &QCheckBox::toggled, this, [this](bool on) {
        if (m_loading)
            return;
        Tweaks::setShadersEnabled(on);
        applyTweaks(tr("Шейдеры"));
    });
    graphicsLayout->addWidget(m_packs);
    graphicsLayout->addWidget(m_shaders);
    left->addWidget(graphics);
    left->addStretch(1);
    columns->addLayout(left, 1);

    auto* right = new QVBoxLayout;
    right->setSpacing(14);

    auto* memoryCard = card(content);
    auto* memoryLayout = new QVBoxLayout(memoryCard);
    memoryLayout->setContentsMargins(18, 14, 18, 16);
    memoryLayout->setSpacing(8);
    memoryLayout->addWidget(caption(tr("ПАМЯТЬ · ВСЕГО %1 ГБ").arg(totalRamGb()), memoryCard));
    auto* memoryRow = new QHBoxLayout;
    memoryRow->setSpacing(8);
    auto* minus = button(QString(), memoryCard, PixelButton::Icon, gMinus());
    minus->setToolTip(tr("Меньше памяти"));
    minus->setAccessibleName(tr("Меньше памяти"));
    m_memory = new QSpinBox(memoryCard);
    m_memory->setRange(2, qMax(4, totalRamGb()));
    m_memory->setSuffix(tr(" ГБ"));
    m_memory->setMinimumHeight(40);
    m_memory->setAlignment(Qt::AlignCenter);
    m_memory->setFont(theme::spaced(11, 0.2, true));
    auto* plus = button(QString(), memoryCard, PixelButton::Icon, gPlus());
    plus->setToolTip(tr("Больше памяти"));
    plus->setAccessibleName(tr("Больше памяти"));
    connect(minus, &QPushButton::clicked, this, [this] { m_memory->setValue(m_memory->value() - 1); });
    connect(plus, &QPushButton::clicked, this, [this] { m_memory->setValue(m_memory->value() + 1); });
    connect(m_memory, &QSpinBox::valueChanged, this, &Panel::onMemoryChanged);
    memoryRow->addWidget(minus);
    memoryRow->addWidget(m_memory, 1);
    memoryRow->addWidget(plus);
    memoryLayout->addLayout(memoryRow);
    right->addWidget(memoryCard);

    auto* javaCard = card(content);
    auto* javaLayout = new QVBoxLayout(javaCard);
    javaLayout->setContentsMargins(18, 14, 18, 16);
    javaLayout->setSpacing(8);
    javaLayout->addWidget(caption(tr("JAVA"), javaCard));
    m_javaPath = new QLineEdit(javaCard);
    m_javaPath->setPlaceholderText(tr("Автоматически"));
    m_javaPath->setMinimumHeight(38);
    connect(m_javaPath, &QLineEdit::editingFinished, this, &Panel::onJavaEdited);
    javaLayout->addWidget(m_javaPath);
    auto* javaRow = new QHBoxLayout;
    javaRow->setSpacing(8);
    auto* browse = button(tr("Обзор…"), javaCard, PixelButton::Normal, gFolder());
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Выберите Java"), m_javaPath->text(),
                                                          QStringLiteral("javaw.exe (javaw.exe);;Все файлы (*)"), nullptr, QFileDialog::DontUseNativeDialog);
        if (path.isEmpty())
            return;
        m_javaPath->setText(QDir::toNativeSeparators(path));
        onJavaEdited();
    });
    auto* autoJava = button(tr("Авто"), javaCard, PixelButton::Ghost);
    connect(autoJava, &QPushButton::clicked, this, [this] {
        m_javaPath->clear();
        m_instance->settings()->set(QStringLiteral("OverrideJavaLocation"), false);
        m_settingsStatus->setText(tr("Java выбирается Prism автоматически."));
    });
    javaRow->addWidget(browse);
    javaRow->addWidget(autoJava);
    javaRow->addStretch(1);
    javaLayout->addLayout(javaRow);
    right->addWidget(javaCard);

    auto* actionsCard = card(content);
    auto* actionsLayout = new QVBoxLayout(actionsCard);
    actionsLayout->setContentsMargins(18, 14, 18, 16);
    actionsLayout->setSpacing(8);
    actionsLayout->addWidget(caption(tr("ДЕЙСТВИЯ"), actionsCard));
    auto* apply = button(tr("Применить"), actionsCard, PixelButton::Primary, gCheck());
    apply->setMinimumHeight(38);
    connect(apply, &QPushButton::clicked, this, [this] { applyTweaks(tr("Настройки")); });
    auto* reset = button(tr("Сбросить конфиги"), actionsCard, PixelButton::Normal, gTrash());
    reset->setMinimumHeight(38);
    connect(reset, &QPushButton::clicked, this, [this] {
        const QString backup = Tweaks::resetConfigs();
        if (backup.isEmpty()) {
            m_settingsStatus->setText(tr("Не удалось сбросить конфиги."));
            return;
        }
        m_settingsStatus->setText(tr("Конфиги сброшены. Копия: %1").arg(backup));
        startSync();
    });
    actionsLayout->addWidget(apply);
    actionsLayout->addWidget(reset);
    m_settingsStatus = new QLabel(tr("Изменения применяются сразу."), actionsCard);
    m_settingsStatus->setObjectName(QStringLiteral("berryDim"));
    m_settingsStatus->setWordWrap(true);
    actionsLayout->addWidget(m_settingsStatus);
    right->addWidget(actionsCard);
    right->addStretch(1);
    columns->addLayout(right, 1);

    layout->addLayout(columns);
    layout->addStretch(1);

    return page(content);
}

QWidget* Panel::buildDiagnostics()
{
    auto* content = new QWidget;
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(26, 20, 26, 20);
    layout->setSpacing(14);

    layout->addWidget(pageHeader(content, tr("ДИАГНОСТИКА"), tr("Состояние"),
                                 tr("Всё, что нужно, чтобы понять, почему игра не запускается.")));

    auto* status = card(content);
    auto* grid = new QGridLayout(status);
    grid->setContentsMargins(18, 14, 18, 16);
    grid->setHorizontalSpacing(18);
    grid->setVerticalSpacing(0);
    const QStringList keys{ tr("Лаунчер"), tr("Экземпляр"), tr("Java"), tr("Моды"),
                            tr("Сборка"), tr("Память"), tr("Диск") };
    for (int i = 0; i < keys.size(); ++i) {
        auto* key = caption(keys.at(i), status);
        key->setMinimumWidth(118);
        auto* value = new QLabel(QStringLiteral("—"), status);
        value->setObjectName(QStringLiteral("berryMono"));
        value->setWordWrap(true);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        value->setMinimumHeight(26);
        value->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
        m_diagValues.append(value);
        grid->addWidget(key, i * 2, 0, Qt::AlignVCenter);
        grid->addWidget(value, i * 2, 1, Qt::AlignVCenter);
        if (i + 1 < keys.size()) {
            auto* line = rule(status);
            line->setStyleSheet(QStringLiteral("background:#151B24; border:none;"));
            grid->addWidget(line, i * 2 + 1, 0, 1, 2);
        }
    }
    grid->setColumnStretch(1, 1);
    layout->addWidget(status);

    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    auto* refresh = button(tr("Обновить"), content, PixelButton::Normal, gRefresh());
    connect(refresh, &QPushButton::clicked, this, &Panel::refreshDiagnostics);
    auto* folder = button(tr("Открыть папку"), content, PixelButton::Normal, gFolder());
    connect(folder, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(Config::gameRoot()));
    });
    auto* copy = button(tr("Скопировать отчёт"), content, PixelButton::Primary, gCopy());
    connect(copy, &QPushButton::clicked, this, [this] {
        const QStringList keys{ tr("Лаунчер"), tr("Экземпляр"), tr("Java"), tr("Моды"),
                                tr("Сборка"), tr("Память"), tr("Диск") };
        QStringList report;
        report << QStringLiteral("BerryTrip %1 · Minecraft 1.21.1 · NeoForge 21.1.267").arg(Config::version());
        for (int i = 0; i < keys.size() && i < m_diagValues.size(); ++i)
            report << QStringLiteral("%1: %2").arg(keys.at(i), m_diagValues.at(i)->text());
        QApplication::clipboard()->setText(report.join(QLatin1Char('\n')));
        if (m_diagStatus)
            m_diagStatus->setText(tr("Отчёт скопирован — его можно отправить в чат."));
    });
    buttons->addWidget(refresh);
    buttons->addWidget(folder);
    buttons->addStretch(1);
    buttons->addWidget(copy);
    layout->addLayout(buttons);
    m_diagStatus = new QLabel(tr("Отчёт собирается из текущих значений."), content);
    m_diagStatus->setObjectName(QStringLiteral("berryDim"));
    m_diagStatus->setWordWrap(true);
    layout->addWidget(m_diagStatus);
    layout->addStretch(1);

    return page(content);
}

QWidget* Panel::buildAbout()
{
    auto* content = new QWidget;
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(26, 18, 26, 18);
    layout->setSpacing(14);

    layout->addWidget(pageHeader(content, tr("О ЛАУНЧЕРЕ"), tr("BerryTrip"),
                                 tr("Неофициальный форк Prism Launcher для сборки BerryTrip.")));

    auto* head = new PixelFrame(PixelFrame::Hero, content);
    auto* headRow = new QHBoxLayout(head);
    headRow->setContentsMargins(18, 14, 18, 18);
    headRow->setSpacing(18);
    auto* logo = new QLabel(head);
    logo->setPixmap(QPixmap(QStringLiteral(":/berry/app-icon.png")).scaled(72, 72, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    logo->setFixedSize(72, 72);
    logo->setScaledContents(true);
    headRow->addWidget(logo, 0, Qt::AlignVCenter);
    auto* text = new QVBoxLayout;
    text->setSpacing(4);
    auto* title = new QLabel(QStringLiteral("BERRYTRIP %1").arg(Config::version()), head);
    title->setObjectName(QStringLiteral("berryTitle"));
    title->setFont(theme::spaced(14, 0.4, true));
    title->setWordWrap(true);
    auto* sub = new QLabel(QStringLiteral("MINECRAFT 1.21.1 · NEOFORGE 21.1.267"), head);
    sub->setObjectName(QStringLiteral("berryCaption"));
    auto* chips = new QHBoxLayout;
    chips->setSpacing(6);
    auto* chipPixel = new QLabel(head);
    setChip(chipPixel, tr("PIXEL UI"), ToneAccent);
    auto* chipFree = new QLabel(head);
    setChip(chipFree, tr("БЕЗ MICROSOFT"), ToneOk);
    chips->addWidget(chipPixel);
    chips->addWidget(chipFree);
    chips->addStretch(1);
    text->addWidget(title);
    text->addWidget(sub);
    text->addSpacing(6);
    text->addLayout(chips);
    headRow->addLayout(text, 1);
    layout->addWidget(head);

    auto* columns = new QHBoxLayout;
    columns->setSpacing(14);

    auto* links = card(content);
    auto* linksLayout = new QVBoxLayout(links);
    linksLayout->setContentsMargins(18, 12, 18, 14);
    linksLayout->setSpacing(2);
    linksLayout->addWidget(caption(tr("ССЫЛКИ"), links));
    linksLayout->addSpacing(4);

    const auto addLink = [&](const QString& text, const QString& url) {
        auto* b = button(QStringLiteral("  %1").arg(text), links, PixelButton::Ghost, gExternal());
        b->setFixedHeight(28);
        connect(b, &QPushButton::clicked, this, [url] { QDesktopServices::openUrl(QUrl(url)); });
        linksLayout->addWidget(b);
    };
    addLink(tr("Сайт сборки"), QString::fromLatin1(SiteUrl));
    addLink(tr("Telegram-бот"), QString::fromLatin1(TelegramUrl));
    addLink(tr("Релизы лаунчера"), QString::fromLatin1(ReleasesUrl));
    addLink(tr("Исходники (GPL-3.0)"), QString::fromLatin1(SourcesUrl));
    addLink(tr("Prism Launcher — оригинал (GPL-3.0)"), QString::fromLatin1(UpstreamUrl));
    linksLayout->addStretch(1);
    columns->addWidget(links, 1);

    auto* right = new QVBoxLayout;
    right->setSpacing(14);

    auto* updateCard = card(content);
    auto* updateLayout = new QVBoxLayout(updateCard);
    updateLayout->setContentsMargins(18, 12, 18, 14);
    updateLayout->setSpacing(8);
    updateLayout->addWidget(caption(tr("ОБНОВЛЕНИЕ"), updateCard));
    auto* updateButtons = new QHBoxLayout;
    updateButtons->setSpacing(8);
    auto* check = button(tr("Проверить обновление"), updateCard, PixelButton::Primary, gDownload());
    connect(check, &QPushButton::clicked, this, &Panel::checkUpdates);
    updateButtons->addWidget(check);
    m_aboutUpdate = button(tr("Скачать обновление"), updateCard, PixelButton::Normal, gDownload());
    m_aboutUpdate->hide();
    connect(m_aboutUpdate, &QPushButton::clicked, this, &Panel::installUpdate);
    updateButtons->addWidget(m_aboutUpdate);
    updateButtons->addStretch(1);
    updateLayout->addLayout(updateButtons);
    m_aboutStatus = new QLabel(tr("Сборка обновляется сама при запуске."), updateCard);
    m_aboutStatus->setObjectName(QStringLiteral("berryDim"));
    m_aboutStatus->setWordWrap(true);
    updateLayout->addWidget(m_aboutStatus);
    right->addWidget(updateCard);

    auto* legal = card(content);
    auto* legalLayout = new QVBoxLayout(legal);
    legalLayout->setContentsMargins(18, 12, 18, 14);
    legalLayout->setSpacing(6);
    legalLayout->addWidget(caption(tr("ЛИЦЕНЗИЯ"), legal));
    auto* legalText = new QLabel(
        tr("GNU GPL v3. Исходный код: github.com/evilvalue/prism-berry, ветка berry, тег v%1 — он "
           "соответствует полученному файлу. © 2022-2026 Prism Launcher Contributors, © 2021-2022 PolyMC "
           "Contributors, © 2012-2021 MultiMC Contributors, © 2026 BerryTrip.\n\n"
           "Minecraft — торговая марка Mojang Studios. Проект не связан с Mojang и Microsoft.")
            .arg(Config::version()),
        legal);
    legalText->setObjectName(QStringLiteral("berryDim"));
    legalText->setWordWrap(true);
    legalLayout->addWidget(legalText);
    right->addWidget(legal);
    right->addStretch(1);

    columns->addLayout(right, 1);
    layout->addLayout(columns);
    layout->addStretch(1);

    return page(content);
}

bool Panel::signedIn() const
{
    return m_auth && m_auth->tokenState() == TelegramAuth::TokenState::Ok;
}

void Panel::goToPage(int index)
{
    m_pages->setCurrentIndex(index);
    if (m_nav && m_nav->button(index))
        m_nav->button(index)->setChecked(true);
    if (index == 1)
        refreshAuth();
    if (index == 2)
        refreshSkins();
    if (index == 4)
        refreshDiagnostics();
}

void Panel::refreshAuth()
{
    const auto state = m_auth->tokenState();
    const bool pending = m_auth->pending();
    const QString nick = m_auth->tokenNick().isEmpty() ? m_auth->nick() : m_auth->tokenNick();

    if (state == TelegramAuth::TokenState::Ok) {
        m_homeAccount->setText(tr("%1\nвход выполнен").arg(nick));
        m_homeAccount->setStyleSheet(QStringLiteral("color:#4BE39F;"));
        m_login->setText(tr("Войти заново"));
        m_logout->setVisible(true);
    } else {
        m_homeAccount->setText(pending ? tr("Ожидает\nподтверждения") : tr("Вход\nне выполнен"));
        m_homeAccount->setStyleSheet(pending ? QStringLiteral("color:#FFA36B;") : QStringLiteral("color:#A6B0C2;"));
        m_login->setText(tr("Войти через Telegram"));
        m_logout->setVisible(pending);
    }

    if (pending) {
        m_login->setText(tr("Открыть Telegram"));
        m_openTelegram->setVisible(true);
        if (!m_pollTimer->isActive())
            m_pollTimer->start();
    } else {
        m_openTelegram->setVisible(false);
        if (m_pollTimer && m_pollTimer->isActive())
            m_pollTimer->stop();
    }

    if (state == TelegramAuth::TokenState::Expired)
        m_authStatus->setText(tr("Сессия истекла — войди снова."));
    else if (state == TelegramAuth::TokenState::OtherNick)
        m_authStatus->setText(tr("Токен выдан другому нику."));
    else if (state == TelegramAuth::TokenState::Broken)
        m_authStatus->setText(tr("Файл токена повреждён — войди снова."));

    refreshLaunch();
}

void Panel::onAuthClicked()
{
    if (m_auth->pending()) {
        if (m_auth->openPendingLink())
            m_authStatus->setText(tr("Открываю Telegram — нажми Start в боте."));
        else
            m_authStatus->setText(tr("Не удалось открыть Telegram — скопируй ссылку кнопкой ниже."));
        return;
    }

    const QString nick = m_nick->text().trimmed();
    if (nick.isEmpty()) {
        m_authStatus->setText(tr("Введи ник — 3–16 символов, латиница, цифры и _."));
        return;
    }

    m_authStatus->setText(tr("Запрашиваю ссылку у сервера…"));
    m_auth->start(nick);
}

void Panel::refreshLaunch()
{
    if (!m_instance) {
        if (m_play)
            m_play->setEnabled(false);
        return;
    }

    const bool running = m_instance->isRunning();
    m_play->setText(running ? tr("Запущено") : tr("Играть"));
    m_play->setEnabled(!running && m_syncFinished && signedIn());

    if (!m_chip)
        return;
    if (running) {
        m_chip->setText(tr("ИГРА ЗАПУЩЕНА"));
        m_chip->setStyleSheet(QStringLiteral("color:#FF5D86;"));
    } else if (m_syncFinished && signedIn()) {
        m_chip->setText(tr("ГОТОВО К ИГРЕ"));
        m_chip->setStyleSheet(QStringLiteral("color:#4BE39F;"));
    } else {
        m_chip->setText(tr("НУЖЕН ВХОД И ОБНОВЛЕНИЕ"));
        m_chip->setStyleSheet(QStringLiteral("color:#F5B942;"));
    }
}

void Panel::startSync()
{
    if (!m_instance || (m_sync && m_sync->isRunning()))
        return;

    if (m_instance->isRunning()) {
        m_status->setText(tr("Сборка обновится после закрытия Minecraft."));
        return;
    }

    m_syncFinished = false;
    refreshLaunch();

    if (!m_sync) {
        m_sync = new PackSync(this);
        connect(m_sync, &PackSync::progress, this, [this](int done, int total, const QString& file) {
            m_progress->setMaximum(total);
            m_progress->setValue(done);
            m_statusBase = tr("Обновление: %1 (%2/%3)").arg(QFileInfo(file).fileName()).arg(done).arg(total);
            m_status->setText(m_statusBase);
        });
        connect(m_sync, &PackSync::finished, this, [this](bool ok, const QString& error) {
            m_progress->hide();
            m_retry->setVisible(!ok);
            if (!ok) {
                // Never let the player start the game on a half-updated pack.
                m_syncFinished = false;
                m_status->setText(tr("Не удалось обновить: %1").arg(error));
                refreshLaunch();
                refreshDiagnostics();
                return;
            }

            // Moving optional mods and rewriting configs can touch a lot of
            // files; do it after the window has painted the completed sync.
            m_status->setText(tr("Применяю настройки…"));
            auto* apply = new QFutureWatcher<void>(this);
            connect(apply, &QFutureWatcher<void>::finished, this, [this, apply] {
                apply->deleteLater();
                m_syncFinished = true;
                m_status->setText(tr("Сборка готова"));
                refreshLaunch();
                refreshDiagnostics();
                refreshSkins();
                if (m_autoInstallPending && m_updateReady)
                    tryAutoInstall();
            });
            apply->setFuture(QtConcurrent::run([] { Tweaks::applyAll(); }));
        });
    }

    m_progress->setMaximum(0);
    m_progress->setValue(0);
    m_progress->show();
    m_retry->hide();
    m_statusBase = tr("Проверка файлов сборки…");
    m_status->setText(m_statusBase);
    m_sync->start();
}

void Panel::startPing()
{
    m_ping->query(Config::serverHost(), Config::serverPort());
}

void Panel::onMemoryChanged(int gigabytes)
{
    if (m_loading || !m_instance)
        return;

    auto* settings = m_instance->settings();
    settings->set(QStringLiteral("OverrideMemory"), true);
    settings->set(QStringLiteral("MaxMemAlloc"), gigabytes * 1024);
    settings->set(QStringLiteral("MinMemAlloc"), qMin(2048, gigabytes * 512));
}

void Panel::onJavaEdited()
{
    if (m_loading || !m_instance)
        return;

    auto* settings = m_instance->settings();
    const QString path = m_javaPath->text().trimmed();
    if (path.isEmpty()) {
        settings->set(QStringLiteral("OverrideJavaLocation"), false);
        return;
    }
    settings->set(QStringLiteral("JavaPath"), path);
    settings->set(QStringLiteral("OverrideJavaLocation"), true);
}

void Panel::applyTweaks(const QString& note)
{
    Tweaks::applyAll();
    if (m_settingsStatus)
        m_settingsStatus->setText(tr("%1 · применено").arg(note));
    refreshDiagnostics();
}

void Panel::refreshDiagnostics()
{
    if (m_diagValues.isEmpty())
        return;

    const QString root = APPLICATION->dataRoot();
    const QString java = m_instance ? m_instance->settings()->get(QStringLiteral("JavaPath")).toString() : QString();
    const int memory = m_instance ? m_instance->settings()->get(QStringLiteral("MaxMemAlloc")).toInt() / 1024 : 0;
    const QStorageInfo storage(root);

    const QStringList values{
        QStringLiteral("%1 %2 · Qt %3").arg(Config::instanceName(), Config::version(), QLatin1String(qVersion())),
        m_instance ? QStringLiteral("%1 · модов: %2").arg(Config::instanceRoot(), QString::number(Tweaks::modCount()))
                   : tr("не создан"),
        java.isEmpty() ? tr("автоматически (Prism)") : QDir::toNativeSeparators(java),
        Tweaks::summary(),
        QStringLiteral("BerryTrip · Minecraft 1.21.1 · NeoForge 21.1.267"),
        tr("%1 ГБ выделено · всего %2 ГБ").arg(memory).arg(totalRamGb()),
        tr("%1 ГБ свободно · %2").arg(humanGb(storage.bytesAvailable()), QDir::toNativeSeparators(root)),
    };

    for (int i = 0; i < m_diagValues.size() && i < values.size(); ++i)
        m_diagValues.at(i)->setText(values.at(i));
}

void Panel::checkUpdates()
{
    if (!m_updater || m_updater->isDownloading())
        return;
    if (m_aboutStatus) {
        m_aboutStatus->setText(tr("Проверяю GitHub…"));
        m_aboutStatus->setStyleSheet(QStringLiteral("color:#7C8798;"));
    }
    m_updater->check();
}

void Panel::installUpdate()
{
    if (!m_updater)
        return;

    if (m_updateReady) {
        if (m_instance && m_instance->isRunning()) {
            const QString message = tr("Сначала закрой Minecraft — обновление заменит файлы лаунчера.");
            if (m_updateText)
                m_updateText->setText(message);
            if (m_aboutStatus)
                m_aboutStatus->setText(message);
            return;
        }
        m_autoInstallPending = false;
        // Stop writing pack files before the installer replaces the launcher.
        if (m_sync && m_sync->isRunning())
            m_sync->abort();
        QString error;
        if (!m_updater->install(&error)) {
            if (m_updateText)
                m_updateText->setText(error);
            if (m_aboutStatus)
                m_aboutStatus->setText(error);
            return;
        }
        QApplication::quit();
        return;
    }

    if (!m_updateCanAuto) {
        if (!m_updatePageUrl.isEmpty())
            QDesktopServices::openUrl(QUrl(m_updatePageUrl));
        return;
    }
    m_updater->download();
}

void Panel::tryAutoInstall()
{
    if (!m_autoInstallPending || !m_updateReady)
        return;

    if (m_instance && m_instance->isRunning()) {
        const QString message = tr("Обновление установится после закрытия Minecraft.");
        if (m_updateText)
            m_updateText->setText(message);
        if (m_aboutStatus)
            m_aboutStatus->setText(message);
        return;
    }

    m_autoInstallPending = false;
    installUpdate();
}

void Panel::onPlay()
{
    if (!m_instance || m_instance->isRunning())
        return;

    if (!signedIn()) {
        goToPage(1);
        m_authStatus->setText(tr("Сначала войди через Telegram-бота."));
        return;
    }

    Tweaks::applyAll();
    auto target = std::make_shared<MinecraftTarget>(MinecraftTarget::parse(Config::serverAddress(), false));
    if (!APPLICATION->launch(m_instance, LaunchMode::Normal, target))
        m_status->setText(tr("Запуск не удался — проверь аккаунт и Java"));
}

void installSoloUi(QMainWindow* window)
{
    // One window, one tray, one frame. Static guards stop duplicate trays
    // (5 ghost icons) when MainWindow is ever re-created.
    static QSystemTrayIcon* g_tray = nullptr;
    static bool g_filtersInstalled = false;
    static bool g_applied = false;

    if (g_applied)
        return;
    g_applied = true;

    theme::applyApplication();
    window->setWindowIcon(theme::appIcon());
    window->setWindowTitle(QStringLiteral("BerryTrip %1").arg(Config::version()));
    // Own pixel frame instead of the native Windows one.
    window->setWindowFlags((window->windowFlags() & ~Qt::WindowMinimizeButtonHint & ~Qt::WindowMaximizeButtonHint) | Qt::FramelessWindowHint | Qt::Window);
#ifdef Q_OS_WIN
    // Keep the old BerryTrip executable icon in the taskbar/Alt-Tab.
    window->setAttribute(Qt::WA_TranslucentBackground, false);
#endif

    // Only hide the Prism chrome. Deleting these widgets is not an option:
    // MainWindow keeps raw pointers to its toolbars and crashes on next use.
    if (auto* bar = window->menuBar())
        bar->hide();
    if (auto* bar = window->statusBar())
        bar->hide();
    for (auto* bar : window->findChildren<QToolBar*>())
        bar->hide();
    for (auto* dock : window->findChildren<QDockWidget*>())
        dock->hide();
    if (auto* central = window->centralWidget())
        central->hide();

    auto* panel = new Panel(window);
    panel->setGeometry(window->rect());
    panel->show();
    panel->raise();

    if (!g_filtersInstalled) {
        g_filtersInstalled = true;
        window->installEventFilter(new OverlayFilter(panel, window));
        qApp->installEventFilter(new PopupFilter(panel, qApp));
        window->installEventFilter(new PrismChromeFilter(window, window));
    }
    QTimer::singleShot(0, window, [panel] { panel->setGeometry(panel->parentWidget()->rect()); });

    window->setFixedSize(1240, 760);

    if (!g_tray && QSystemTrayIcon::isSystemTrayAvailable()) {
        qApp->setQuitOnLastWindowClosed(false);
        g_tray = new QSystemTrayIcon(window);
        g_tray->setIcon(theme::appIcon());
        g_tray->setToolTip(QStringLiteral("BerryTrip %1").arg(Config::version()));
        auto* menu = new QMenu(window);
        auto* open = menu->addAction(QObject::tr("Открыть"));
        auto* hide = menu->addAction(QObject::tr("Свернуть в трей"));
        menu->addSeparator();
        auto* quit = menu->addAction(QObject::tr("Выход"));
        g_tray->setContextMenu(menu);
        const auto restore = [window] {
            window->showNormal();
            window->raise();
            window->activateWindow();
        };
        QObject::connect(open, &QAction::triggered, window, restore);
        QObject::connect(hide, &QAction::triggered, window, [window] { window->hide(); });
        QObject::connect(quit, &QAction::triggered, qApp, &QApplication::quit);
        QObject::connect(g_tray, &QSystemTrayIcon::activated, window,
                         [restore](QSystemTrayIcon::ActivationReason reason) {
                             if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
                                 restore();
                         });
        g_tray->show();
        window->installEventFilter(new TrayFilter(window, g_tray));
        QObject::connect(qApp, &QApplication::aboutToQuit, window, [] {
            if (g_tray) g_tray->hide();
        });
    } else if (g_tray) {
        g_tray->show();
    }
}

}  // namespace berry
