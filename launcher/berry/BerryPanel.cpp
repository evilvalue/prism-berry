// SPDX-License-Identifier: GPL-3.0-only
/*
 *  BerryTrip Launcher - single server build of Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "berry/BerryPanel.h"

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

QFrame* card(QWidget* parent)
{
    auto* frame = new QFrame(parent);
    frame->setObjectName(QStringLiteral("berryCard"));
    return frame;
}

QLabel* caption(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("berryCaption"));
    QFont font = theme::regular(10);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
    label->setFont(font);
    return label;
}

QLabel* sectionLabel(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("berryH2"));
    return label;
}

QFrame* rule(QWidget* parent)
{
    auto* line = new QFrame(parent);
    line->setObjectName(QStringLiteral("berryRule"));
    line->setFixedHeight(2);
    return line;
}

QPushButton* button(const QString& text, QWidget* parent, const QString& objectName = QString())
{
    auto* b = new QPushButton(text, parent);
    b->setAutoDefault(false);
    b->setCursor(Qt::PointingHandCursor);
    if (!objectName.isEmpty())
        b->setObjectName(objectName);
    return b;
}

QWidget* pageHeader(QWidget* parent, const QString& eyebrow, const QString& title, const QString& subtitle)
{
    auto* wrap = new QWidget(parent);
    auto* box = new QVBoxLayout(wrap);
    box->setContentsMargins(0, 0, 0, 2);
    box->setSpacing(5);
    box->addWidget(caption(eyebrow, wrap));

    auto* heading = new QLabel(title, wrap);
    heading->setObjectName(QStringLiteral("berryTitle"));
    QFont font = theme::semiBold(26);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 0.5);
    heading->setFont(font);
    box->addWidget(heading);

    if (!subtitle.isEmpty()) {
        auto* sub = new QLabel(subtitle, wrap);
        sub->setObjectName(QStringLiteral("berryMuted"));
        sub->setWordWrap(true);
        box->addWidget(sub);
    }

    box->addSpacing(8);
    box->addWidget(rule(wrap));
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



class NavButton : public QPushButton {
   public:
    NavButton(const QString& label, const QStringList& glyph, QWidget* parent)
        : QPushButton(label, parent), m_glyph(glyph)
    {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_Hover, true);
        QFont navFont = theme::semiBold(11);
        navFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
        setFont(navFont);
        setFixedHeight(46);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

   protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, false);

        const QRectF area = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const bool active = isChecked();
        const bool hover = underMouse();
        const QColor background = active ? QColor(0x19, 0x12, 0x1D) : hover ? QColor(0x12, 0x17, 0x20) : QColor(0x0C, 0x0E, 0x14);
        const QColor border = active ? QColor(0xFF, 0x5D, 0x86) : hover ? QColor(0x2C, 0x33, 0x40) : QColor(0, 0, 0, 0);

        const qreal cut = 4.0;
        QPainterPath shape;
        shape.moveTo(area.left() + cut, area.top());
        shape.lineTo(area.right() - cut, area.top());
        shape.lineTo(area.right(), area.top() + cut);
        shape.lineTo(area.right(), area.bottom() - cut);
        shape.lineTo(area.right() - cut, area.bottom());
        shape.lineTo(area.left() + cut, area.bottom());
        shape.lineTo(area.left(), area.bottom() - cut);
        shape.lineTo(area.left(), area.top() + cut);
        shape.closeSubpath();
        painter.fillPath(shape, background);

        if (border.alpha() > 0) {
            painter.setPen(QPen(border, 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(shape);
        }
        if (active)
            painter.fillRect(QRectF(area.left() + 2, area.top() + 7, 4, area.height() - 14), QColor(0xFF, 0x5D, 0x86));

        const QColor icon = active ? QColor(0xFF, 0x5D, 0x86) : hover ? QColor(0xE9, 0xED, 0xF5) : QColor(0x8A, 0x94, 0xA6);
        const qreal pixel = 2.0;
        const qreal iconX = 16.0;
        const qreal iconY = (height() - m_glyph.size() * pixel) / 2.0;
        painter.setPen(Qt::NoPen);
        for (int row = 0; row < m_glyph.size(); ++row) {
            const QString& line = m_glyph.at(row);
            for (int column = 0; column < line.size(); ++column) {
                if (line.at(column) == QLatin1Char('#'))
                    painter.fillRect(QRectF(iconX + column * pixel, iconY + row * pixel, pixel, pixel), icon);
            }
        }

        painter.setFont(font());
        painter.setPen(active ? QColor(0xF7, 0xF9, 0xFC) : hover ? QColor(0xE9, 0xED, 0xF5) : QColor(0xA6, 0xB0, 0xC2));
        painter.drawText(QRect(48, 0, width() - 62, height()), Qt::AlignVCenter | Qt::AlignLeft, text());

        if (active)
            painter.fillRect(QRectF(width() - 20, height() / 2.0 - 2, 4, 4), QColor(0xFF, 0x5D, 0x86));
    }

   private:
    QStringList m_glyph;
};

QStringList navGlyph(int index)
{
    switch (index) {
        case 0:
            return { QStringLiteral("....##...."), QStringLiteral("...####..."), QStringLiteral("..######.."),
                     QStringLiteral(".########."), QStringLiteral("##########"), QStringLiteral("...####..."),
                     QStringLiteral("...####..."), QStringLiteral("...####..."), QStringLiteral("...####..."),
                     QStringLiteral("...####...") };
        case 1:
            return { QStringLiteral("...##....."), QStringLiteral("..####...."), QStringLiteral("..####...."),
                     QStringLiteral("...##....."), QStringLiteral(".######..."), QStringLiteral("########.."),
                     QStringLiteral("########.."), QStringLiteral("...##....."), QStringLiteral("...##....."),
                     QStringLiteral("..####....") };
        case 2:
            return { QStringLiteral("##.....##."), QStringLiteral("###...###."), QStringLiteral("##########"),
                     QStringLiteral(".########."), QStringLiteral("...####..."), QStringLiteral("...####..."),
                     QStringLiteral("...####..."), QStringLiteral("...####..."), QStringLiteral("...####..."),
                     QStringLiteral("...####...") };
        case 3:
            return { QStringLiteral("###...####"), QStringLiteral("###...####"), QStringLiteral(".........."),
                     QStringLiteral("####...###"), QStringLiteral("####...###"), QStringLiteral(".........."),
                     QStringLiteral("###...####"), QStringLiteral("###...####"), QStringLiteral(".........."),
                     QStringLiteral("..........") };
        case 4:
            return { QStringLiteral(".....##..."), QStringLiteral(".....##..."), QStringLiteral("...###...."),
                     QStringLiteral("...###...."), QStringLiteral("####..####"), QStringLiteral("..###....."),
                     QStringLiteral("....###..."), QStringLiteral(".....##..."), QStringLiteral(".....##..."),
                     QStringLiteral("..........") };
        default:
            return { QStringLiteral("...###...."), QStringLiteral("..#####..."), QStringLiteral("..#####..."),
                     QStringLiteral("....##...."), QStringLiteral("....##...."), QStringLiteral("....##...."),
                     QStringLiteral(".........."), QStringLiteral("....##...."), QStringLiteral("....##...."),
                     QStringLiteral("..........") };
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

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---------------------------------------------------------------- sidebar
    auto* side = new QWidget(this);
    side->setObjectName(QStringLiteral("berrySide"));
    side->setFixedWidth(238);
    side->setStyleSheet(
        QStringLiteral("#berrySide { background: #0B0D12; background-image: url(:/berry/pattern.png); "
                       "border-right: 2px solid #1C222D; }"));
    auto* sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(16, 22, 16, 16);
    sideLayout->setSpacing(4);

    auto* logo = new QLabel(side);
    logo->setPixmap(QPixmap(QStringLiteral(":/berry/berry.png"))
                        .scaled(54, 54, Qt::KeepAspectRatio, Qt::FastTransformation));
    sideLayout->addWidget(logo);

    auto* wordmark = new QLabel(QStringLiteral("BERRYTRIP"), side);
    wordmark->setObjectName(QStringLiteral("berryH1"));
    QFont wordFont = theme::semiBold(18);
    wordFont.setLetterSpacing(QFont::AbsoluteSpacing, 2.0);
    wordmark->setFont(wordFont);
    sideLayout->addWidget(wordmark);

    sideLayout->addWidget(caption(QStringLiteral("1.21.1 · NEOFORGE"), side));
    sideLayout->addSpacing(14);
    sideLayout->addWidget(rule(side));
    sideLayout->addSpacing(10);

    m_pages = new QStackedWidget(this);
    m_pages->setObjectName(QStringLiteral("berryPages"));
    m_pages->setAutoFillBackground(true);

    sideLayout->addWidget(caption(tr("РАЗДЕЛЫ"), side));
    sideLayout->addSpacing(4);

    const QStringList titles{ tr("ГЛАВНОЕ"), tr("АККАУНТ"), tr("СКИНЫ"), tr("НАСТРОЙКИ"),
                              tr("ДИАГНОСТИКА"), tr("О ПРОГРАММЕ") };
    m_nav = new QButtonGroup(this);
    m_nav->setExclusive(true);
    for (int i = 0; i < titles.size(); ++i) {
        auto* navButton = new NavButton(titles.at(i), navGlyph(i), side);
        navButton->setChecked(i == 0);
        m_nav->addButton(navButton, i);
        sideLayout->addWidget(navButton);
    }

    sideLayout->addStretch(1);
    sideLayout->addWidget(rule(side));
    sideLayout->addSpacing(8);
    sideLayout->addWidget(caption(QStringLiteral("v%1 · GPL-3.0").arg(Config::version()), side));
    auto* siteButton = button(tr("Сайт сборки"), side, QStringLiteral("berryGhost"));
    connect(siteButton, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl(SiteUrl)); });
    sideLayout->addWidget(siteButton);

    // ---------------------------------------------------------------- pages
    m_pages->addWidget(buildHome());
    m_pages->addWidget(buildAccount());
    m_pages->addWidget(buildSkins());
    m_pages->addWidget(buildSettings());
    m_pages->addWidget(buildDiagnostics());
    m_pages->addWidget(buildAbout());
    m_pages->setCurrentIndex(0);

    connect(m_nav, &QButtonGroup::idClicked, this, &Panel::goToPage);

    root->addWidget(side);
    root->addWidget(m_pages, 1);

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
    for (int i = 0; i < Tweaks::mods().size(); ++i)
        m_modChecks.at(i)->setChecked(Tweaks::modEnabled(Tweaks::mods().at(i)));
    m_packs->setChecked(Tweaks::packsEnabled());
    m_shaders->setChecked(Tweaks::shadersEnabled());
    m_loading = false;

    connect(m_instance, &BaseInstance::runningStatusChanged, this, [this] {
        refreshLaunch();
        if (!m_instance->isRunning())
            tryAutoInstall();
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
    content->setMaximumWidth(1120);
    content->setObjectName(QStringLiteral("berryPage"));
    content->setAttribute(Qt::WA_StyledBackground, true);

    auto* center = new QWidget;
    center->setObjectName(QStringLiteral("berryPage"));
    center->setAttribute(Qt::WA_StyledBackground, true);
    auto* centerBox = new QHBoxLayout(center);
    centerBox->setContentsMargins(0, 0, 0, 0);
    centerBox->setSpacing(0);
    centerBox->addStretch(1);
    centerBox->addWidget(content, 0);
    centerBox->addStretch(1);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(center);
    return scroll;
}

QWidget* Panel::buildHome()
{
    auto* content = new QWidget;
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(32, 26, 32, 26);
    layout->setSpacing(16);

    layout->addWidget(pageHeader(content, tr("BERRYTRIP"), tr("Главное"),
                                 tr("Сборка обновляется сама, вход — через Telegram-бота.")));

    auto* body = new QHBoxLayout;
    body->setSpacing(16);

    auto* left = new QVBoxLayout;
    left->setSpacing(16);

    // ------------------------------------------------------------------ hero
    auto* hero = new QFrame;
    hero->setObjectName(QStringLiteral("berryHero"));
    auto* heroRow = new QHBoxLayout(hero);
    heroRow->setContentsMargins(24, 20, 24, 20);
    heroRow->setSpacing(18);

    auto* logo = new QLabel(hero);
    logo->setPixmap(QPixmap(QStringLiteral(":/berry/berry.png"))
                        .scaled(66, 66, Qt::KeepAspectRatio, Qt::FastTransformation));
    heroRow->addWidget(logo);

    auto* text = new QVBoxLayout;
    text->setSpacing(4);
    auto* title = new QLabel(QStringLiteral("BerryTrip"), hero);
    title->setObjectName(QStringLiteral("berryDisplay"));
    QFont titleFont = theme::semiBold(32);
    titleFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
    title->setFont(titleFont);
    auto* subtitle = new QLabel(QStringLiteral("MINECRAFT 1.21.1 · NEOFORGE 21.1.267"), hero);
    subtitle->setObjectName(QStringLiteral("berryCaption"));
    m_chip = new QLabel(tr("ПРОВЕРКА СБОРКИ"), hero);
    m_chip->setObjectName(QStringLiteral("berryWarn"));
    QFont chipFont = theme::semiBold(10);
    chipFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.4);
    m_chip->setFont(chipFont);
    text->addWidget(title);
    text->addWidget(subtitle);
    text->addSpacing(3);
    text->addWidget(m_chip, 0, Qt::AlignLeft);
    heroRow->addLayout(text, 1);

    m_play = button(tr("Играть"), hero, QStringLiteral("berryPlay"));
    m_play->setMinimumSize(214, 62);
    connect(m_play, &QPushButton::clicked, this, &Panel::onPlay);
    heroRow->addWidget(m_play, 0, Qt::AlignVCenter);
    left->addWidget(hero);

    // ------------------------------------------------------ launcher update
    m_updateBanner = new QFrame(content);
    m_updateBanner->setObjectName(QStringLiteral("berryUpdate"));
    auto* updateBannerLayout = new QVBoxLayout(m_updateBanner);
    updateBannerLayout->setContentsMargins(18, 14, 18, 14);
    updateBannerLayout->setSpacing(8);
    auto* updateHead = new QHBoxLayout;
    updateHead->setSpacing(10);
    updateHead->addWidget(caption(tr("ОБНОВЛЕНИЕ ЛАУНЧЕРА"), m_updateBanner));
    updateHead->addStretch(1);
    m_updateButton = button(tr("Скачать"), m_updateBanner, QStringLiteral("berryPrimary"));
    connect(m_updateButton, &QPushButton::clicked, this, &Panel::installUpdate);
    updateHead->addWidget(m_updateButton);
    updateBannerLayout->addLayout(updateHead);
    m_updateText = new QLabel(tr("Доступна новая версия."), m_updateBanner);
    m_updateText->setObjectName(QStringLiteral("berryMuted"));
    m_updateText->setWordWrap(true);
    updateBannerLayout->addWidget(m_updateText);
    m_updateProgress = new QProgressBar(m_updateBanner);
    m_updateProgress->setTextVisible(false);
    m_updateProgress->hide();
    updateBannerLayout->addWidget(m_updateProgress);
    m_updateBanner->hide();
    left->addWidget(m_updateBanner);

    // --------------------------------------------------------- server/account
    auto* row = new QHBoxLayout;
    row->setSpacing(16);

    auto* server = card(content);
    auto* serverLayout = new QVBoxLayout(server);
    serverLayout->setContentsMargins(20, 16, 20, 16);
    serverLayout->setSpacing(6);
    serverLayout->addWidget(caption(tr("СЕРВЕР"), server));
    auto* address = new QLabel(Config::serverAddress(), server);
    address->setObjectName(QStringLiteral("berryMono"));
    serverLayout->addWidget(address);
    m_motd = new QLabel(tr("Проверяю сервер…"), server);
    m_motd->setObjectName(QStringLiteral("berryH1"));
    m_motd->setWordWrap(true);
    m_players = new QLabel(tr("Игроки: —"), server);
    m_players->setObjectName(QStringLiteral("berryMuted"));
    m_pingLabel = new QLabel(tr("PING —"), server);
    m_pingLabel->setObjectName(QStringLiteral("berryDim"));
    serverLayout->addWidget(m_motd);
    serverLayout->addWidget(m_players);
    serverLayout->addWidget(m_pingLabel);
    serverLayout->addStretch(1);

    auto* serverButtons = new QHBoxLayout;
    serverButtons->setSpacing(8);
    auto* refresh = button(tr("Пинг"), server);
    connect(refresh, &QPushButton::clicked, this, &Panel::startPing);
    auto* telegram = button(tr("Telegram"), server);
    connect(telegram, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl(TelegramUrl)); });
    serverButtons->addWidget(refresh);
    serverButtons->addWidget(telegram);
    serverButtons->addStretch(1);
    serverLayout->addLayout(serverButtons);
    row->addWidget(server, 2);

    auto* account = card(content);
    auto* accountLayout = new QVBoxLayout(account);
    accountLayout->setContentsMargins(20, 16, 20, 16);
    accountLayout->setSpacing(6);
    accountLayout->addWidget(caption(tr("АККАУНТ"), account));
    m_homeAccount = new QLabel(tr("Вход не выполнен"), account);
    m_homeAccount->setObjectName(QStringLiteral("berryH1"));
    m_homeAccount->setWordWrap(true);
    accountLayout->addWidget(m_homeAccount);
    accountLayout->addStretch(1);
    auto* openAccount = button(tr("Аккаунт →"), account);
    connect(openAccount, &QPushButton::clicked, this, [this] { goToPage(1); });
    accountLayout->addWidget(openAccount, 0, Qt::AlignLeft);
    row->addWidget(account, 1);
    left->addLayout(row);

    // ------------------------------------------------------------------ sync
    auto* update = card(content);
    auto* updateLayout = new QVBoxLayout(update);
    updateLayout->setContentsMargins(20, 16, 20, 16);
    updateLayout->setSpacing(8);
    updateLayout->addWidget(caption(tr("СБОРКА"), update));
    m_progress = new QProgressBar(update);
    m_progress->setTextVisible(false);
    m_status = new QLabel(tr("Подготовка…"), update);
    m_status->setObjectName(QStringLiteral("berryDim"));
    m_status->setWordWrap(true);
    updateLayout->addWidget(m_progress);
    updateLayout->addWidget(m_status);

    auto* actions = new QHBoxLayout;
    m_retry = button(tr("Повторить"), update);
    m_retry->hide();
    connect(m_retry, &QPushButton::clicked, this, &Panel::startSync);
    actions->addWidget(m_retry);
    actions->addStretch(1);
    auto* site = button(tr("Сайт"), update, QStringLiteral("berryGhost"));
    connect(site, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl(SiteUrl)); });
    auto* releases = button(tr("Релизы"), update, QStringLiteral("berryGhost"));
    connect(releases, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl(ReleasesUrl)); });
    actions->addWidget(site);
    actions->addWidget(releases);
    updateLayout->addLayout(actions);
    left->addWidget(update);
    left->addStretch(1);

    body->addLayout(left, 1);

    // --------------------------------------------------------------- showcase
    auto* showcase = new QFrame(content);
    showcase->setObjectName(QStringLiteral("berryShowcase"));
    showcase->setFixedWidth(330);
    auto* showcaseLayout = new QVBoxLayout(showcase);
    showcaseLayout->setContentsMargins(16, 14, 16, 14);
    showcaseLayout->setSpacing(8);

    auto* showcaseHead = new QHBoxLayout;
    showcaseHead->addWidget(caption(tr("ТВОЙ СКИН"), showcase));
    showcaseHead->addStretch(1);
    auto* accent = new QLabel(QStringLiteral("◆"), showcase);
    accent->setObjectName(QStringLiteral("berryAccent"));
    showcaseHead->addWidget(accent);
    showcaseLayout->addLayout(showcaseHead);

    m_skinPreview = new Skin3D(showcase);
    m_skinPreview->setMinimumSize(280, 380);
    m_skinPreview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    showcaseLayout->addWidget(m_skinPreview, 1);

    m_skinName = new QLabel(tr("Скин не выбран"), showcase);
    m_skinName->setObjectName(QStringLiteral("berryDim"));
    m_skinName->setWordWrap(true);
    showcaseLayout->addWidget(m_skinName);

    auto* showcaseFoot = new QHBoxLayout;
    auto* openSkins = button(tr("Скины →"), showcase);
    connect(openSkins, &QPushButton::clicked, this, [this] { goToPage(2); });
    showcaseFoot->addWidget(openSkins);
    showcaseFoot->addStretch(1);
    showcaseLayout->addLayout(showcaseFoot);

    body->addWidget(showcase, 0);
    layout->addLayout(body, 1);

    return page(content);
}

QWidget* Panel::buildAccount()
{
    auto* content = new QWidget;
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(32, 26, 32, 26);
    layout->setSpacing(16);

    layout->addWidget(pageHeader(content, tr("АККАУНТ"), tr("Вход"),
                                 tr("Только через Telegram-бота — Microsoft-аккаунт не нужен.")));

    auto* loginCard = card(content);
    loginCard->setMaximumWidth(720);
    auto* loginLayout = new QVBoxLayout(loginCard);
    loginLayout->setContentsMargins(22, 20, 22, 20);
    loginLayout->setSpacing(12);
    loginLayout->addWidget(caption(tr("НИК В MINECRAFT"), loginCard));

    auto* nickRow = new QHBoxLayout;
    nickRow->setSpacing(12);
    m_nick = new QLineEdit(loginCard);
    m_nick->setPlaceholderText(tr("3–16 символов: латиница, цифры, _"));
    m_nick->setMinimumHeight(40);
    nickRow->addWidget(m_nick, 1);
    m_login = button(tr("Войти через Telegram"), loginCard, QStringLiteral("berryPrimary"));
    m_login->setMinimumHeight(40);
    connect(m_login, &QPushButton::clicked, this, &Panel::onAuthClicked);
    nickRow->addWidget(m_login);
    loginLayout->addLayout(nickRow);

    m_authStatus = new QLabel(tr("Введи ник и подтверди вход в боте."), loginCard);
    m_authStatus->setObjectName(QStringLiteral("berryDim"));
    m_authStatus->setWordWrap(true);
    loginLayout->addWidget(m_authStatus);
    loginLayout->addWidget(rule(loginCard));

    auto* secondary = new QHBoxLayout;
    secondary->setSpacing(10);
    m_openTelegram = button(tr("Скопировать ссылку"), loginCard);
    connect(m_openTelegram, &QPushButton::clicked, this, [this] {
        const QString link = m_auth->pendingLink();
        if (link.isEmpty()) {
            m_authStatus->setText(tr("Ссылка ещё не получена — начни вход заново."));
            return;
        }
        QApplication::clipboard()->setText(link);
        m_authStatus->setText(tr("Ссылка скопирована — открой её в Telegram или браузере."));
    });
    m_logout = button(tr("Выйти"), loginCard, QStringLiteral("berryGhost"));
    connect(m_logout, &QPushButton::clicked, this, [this] {
        m_auth->logout();
        refreshAuth();
    });
    m_openTelegram->hide();
    secondary->addWidget(m_openTelegram);
    secondary->addWidget(m_logout);
    secondary->addStretch(1);
    loginLayout->addLayout(secondary);
    layout->addWidget(loginCard, 0, Qt::AlignLeft);

    auto* steps = card(content);
    steps->setMaximumWidth(720);
    auto* stepsLayout = new QVBoxLayout(steps);
    stepsLayout->setContentsMargins(22, 20, 22, 20);
    stepsLayout->setSpacing(10);
    stepsLayout->addWidget(caption(tr("КАК ЭТО РАБОТАЕТ"), steps));

    const QVector<QPair<QString, QString>> rows{
        { QStringLiteral("01"), tr("Введи ник, под которым играешь.") },
        { QStringLiteral("02"), tr("Лаунчер откроет Telegram Desktop или браузер.") },
        { QStringLiteral("03"), tr("Нажми Start в Telegram — ник попадёт в вайтлист.") },
        { QStringLiteral("04"), tr("Токен сохранится, кнопка «Играть» разблокируется.") },
    };
    for (const auto& row : rows) {
        auto* line = new QHBoxLayout;
        line->setSpacing(14);
        auto* number = new QLabel(row.first, steps);
        number->setObjectName(QStringLiteral("berryAccent"));
        number->setFont(theme::semiBold(14));
        number->setFixedWidth(28);
        auto* text = new QLabel(row.second, steps);
        text->setObjectName(QStringLiteral("berryMuted"));
        text->setWordWrap(true);
        line->addWidget(number, 0, Qt::AlignTop);
        line->addWidget(text, 1);
        stepsLayout->addLayout(line);
    }
    layout->addWidget(steps, 0, Qt::AlignLeft);
    layout->addStretch(1);

    return page(content);
}

QWidget* Panel::buildSkins()
{
    auto* content = new QWidget;
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(32, 26, 32, 26);
    layout->setSpacing(16);

    layout->addWidget(pageHeader(content, tr("QUICK SKIN"), tr("Скины"),
                                 tr("Скин ставится прямо в библиотеку Quick Skin — в игре он появится сразу.")));

    auto* modelCard = card(content);
    auto* modelLayout = new QVBoxLayout(modelCard);
    modelLayout->setContentsMargins(20, 16, 20, 16);
    modelLayout->setSpacing(8);
    modelLayout->addWidget(caption(tr("МОДЕЛЬ"), modelCard));

    auto* models = new QHBoxLayout;
    models->setSpacing(6);
    auto* modelGroup = new QButtonGroup(modelCard);
    modelGroup->setExclusive(true);
    const auto addModel = [&](const QString& text, const QString& value) {
        auto* b = button(text, modelCard, QStringLiteral("berrySegment"));
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
    models->addSpacing(10);
    auto* modelHint = new QLabel(tr("3D-модель показывается на главном экране."), modelCard);
    modelHint->setObjectName(QStringLiteral("berryDim"));
    models->addWidget(modelHint);
    models->addStretch(1);
    modelLayout->addLayout(models);
    layout->addWidget(modelCard);

    auto* columns = new QHBoxLayout;
    columns->setSpacing(16);

    auto* skinCard = card(content);
    auto* skinLayout = new QVBoxLayout(skinCard);
    skinLayout->setContentsMargins(20, 16, 20, 16);
    skinLayout->setSpacing(8);
    skinLayout->addWidget(caption(tr("СКИНЫ В БИБЛИОТЕКЕ"), skinCard));
    m_skinList = new QListWidget(skinCard);
    m_skinList->setMinimumHeight(220);
    m_skinList->setSelectionMode(QAbstractItemView::SingleSelection);
    skinLayout->addWidget(m_skinList, 1);
    auto* skinButtons = new QHBoxLayout;
    skinButtons->setSpacing(8);
    auto* pickSkin = button(tr("Выбрать PNG/GIF"), skinCard);
    connect(pickSkin, &QPushButton::clicked, this, &Panel::pickSkin);
    auto* applySkin = button(tr("Применить"), skinCard, QStringLiteral("berryPrimary"));
    connect(applySkin, &QPushButton::clicked, this, &Panel::applySelectedSkin);
    skinButtons->addWidget(pickSkin);
    skinButtons->addWidget(applySkin);
    skinButtons->addStretch(1);
    skinLayout->addLayout(skinButtons);
    columns->addWidget(skinCard, 3);

    auto* capeCard = card(content);
    auto* capeLayout = new QVBoxLayout(capeCard);
    capeLayout->setContentsMargins(20, 16, 20, 16);
    capeLayout->setSpacing(8);
    capeLayout->addWidget(caption(tr("ПЛАЩ"), capeCard));
    m_capeList = new QListWidget(capeCard);
    m_capeList->setMinimumHeight(160);
    m_capeList->setSelectionMode(QAbstractItemView::SingleSelection);
    capeLayout->addWidget(m_capeList);
    m_capeName = new QLabel(tr("Плащ выключен"), capeCard);
    m_capeName->setObjectName(QStringLiteral("berryDim"));
    capeLayout->addWidget(m_capeName);
    m_skinStatus = new QLabel(tr("Quick Skin проверяется…"), capeCard);
    m_skinStatus->setObjectName(QStringLiteral("berryDim"));
    m_skinStatus->setWordWrap(true);
    capeLayout->addWidget(m_skinStatus);

    auto* capeButtons = new QHBoxLayout;
    capeButtons->setSpacing(8);
    auto* pickCape = button(tr("Выбрать плащ"), capeCard);
    connect(pickCape, &QPushButton::clicked, this, &Panel::pickCape);
    auto* applyCape = button(tr("Включить"), capeCard, QStringLiteral("berryPrimary"));
    connect(applyCape, &QPushButton::clicked, this, [this] {
        auto* item = m_capeList ? m_capeList->currentItem() : nullptr;
        if (!item) {
            m_skinStatus->setText(tr("Выбери плащ в списке."));
            return;
        }
        QString error;
        if (!Skins::activateCape(item->data(Qt::UserRole).toString(), &error)) {
            m_skinStatus->setText(error);
            m_skinStatus->setStyleSheet(QStringLiteral("color:#FF6E7B;"));
            return;
        }
        m_skinStatus->setText(tr("Плащ включён: %1").arg(item->text().mid(2)));
        m_skinStatus->setStyleSheet(QStringLiteral("color:#4BE39F;"));
        refreshSkins();
    });
    auto* clearCape = button(tr("Убрать плащ"), capeCard, QStringLiteral("berryGhost"));
    connect(clearCape, &QPushButton::clicked, this, [this] {
        Skins::clearCape();
        m_skinStatus->setText(tr("Плащ выключен."));
        m_skinStatus->setStyleSheet(QStringLiteral("color:#4BE39F;"));
        refreshSkins();
    });
    capeButtons->addWidget(pickCape);
    capeButtons->addWidget(applyCape);
    capeButtons->addWidget(clearCape);
    capeButtons->addStretch(1);
    capeLayout->addLayout(capeButtons);
    columns->addWidget(capeCard, 2);

    layout->addLayout(columns, 1);

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
}

void Panel::pickSkin()
{
    const QString file = QFileDialog::getOpenFileName(this, tr("Скин для Quick Skin"), QString(),
                                                      tr("Изображения (*.png *.gif)"));
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
                                                      tr("Изображения (*.png *.gif)"));
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
    layout->setContentsMargins(32, 26, 32, 26);
    layout->setSpacing(16);

    layout->addWidget(pageHeader(content, tr("НАСТРОЙКИ"), tr("Сборка"),
                                 tr("Моды, графика, память и Java. Применяется сразу.")));

    auto* columns = new QHBoxLayout;
    columns->setSpacing(16);

    auto* left = new QVBoxLayout;
    left->setSpacing(16);

    auto* mods = card(content);
    auto* modsLayout = new QVBoxLayout(mods);
    modsLayout->setContentsMargins(20, 18, 20, 18);
    modsLayout->setSpacing(8);
    modsLayout->addWidget(caption(tr("МОДЫ"), mods));
    for (const Tweaks::Mod& mod : Tweaks::mods()) {
        auto* check = new QCheckBox(mod.label, mods);
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
        modsLayout->addWidget(check);
    }
    modsLayout->addSpacing(4);
    auto* locked = new QLabel(tr("Всегда включены: JourneyMap · Emotecraft · Immersive Paintings · Not Enough Animations"),
                              mods);
    locked->setObjectName(QStringLiteral("berryDim"));
    locked->setWordWrap(true);
    modsLayout->addWidget(locked);
    left->addWidget(mods);

    auto* graphics = card(content);
    auto* graphicsLayout = new QVBoxLayout(graphics);
    graphicsLayout->setContentsMargins(20, 18, 20, 18);
    graphicsLayout->setSpacing(8);
    graphicsLayout->addWidget(caption(tr("ГРАФИКА"), graphics));
    m_packs = new QCheckBox(tr("Ресурспаки"), graphics);
    m_shaders = new QCheckBox(tr("Шейдеры"), graphics);
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
    right->setSpacing(16);

    auto* memoryCard = card(content);
    auto* memoryLayout = new QVBoxLayout(memoryCard);
    memoryLayout->setContentsMargins(20, 18, 20, 18);
    memoryLayout->setSpacing(10);
    memoryLayout->addWidget(caption(tr("ПАМЯТЬ · ВСЕГО %1 ГБ").arg(totalRamGb()), memoryCard));
    auto* memoryRow = new QHBoxLayout;
    memoryRow->setSpacing(8);
    auto* minus = button(QStringLiteral("−"), memoryCard);
    minus->setFixedWidth(42);
    minus->setMinimumHeight(40);
    m_memory = new QSpinBox(memoryCard);
    m_memory->setRange(2, qMax(4, totalRamGb()));
    m_memory->setSuffix(tr(" ГБ"));
    m_memory->setMinimumHeight(40);
    auto* plus = button(QStringLiteral("+"), memoryCard);
    plus->setFixedWidth(42);
    plus->setMinimumHeight(40);
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
    javaLayout->setContentsMargins(20, 18, 20, 18);
    javaLayout->setSpacing(10);
    javaLayout->addWidget(caption(tr("JAVA"), javaCard));
    m_javaPath = new QLineEdit(javaCard);
    m_javaPath->setPlaceholderText(tr("Автоматически"));
    m_javaPath->setMinimumHeight(40);
    connect(m_javaPath, &QLineEdit::editingFinished, this, &Panel::onJavaEdited);
    javaLayout->addWidget(m_javaPath);
    auto* javaRow = new QHBoxLayout;
    javaRow->setSpacing(8);
    auto* browse = button(tr("Обзор…"), javaCard);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Выберите Java"), m_javaPath->text(),
                                                          QStringLiteral("javaw.exe (javaw.exe);;Все файлы (*)"));
        if (path.isEmpty())
            return;
        m_javaPath->setText(QDir::toNativeSeparators(path));
        onJavaEdited();
    });
    auto* autoJava = button(tr("Авто"), javaCard);
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
    actionsLayout->setContentsMargins(20, 18, 20, 18);
    actionsLayout->setSpacing(10);
    actionsLayout->addWidget(caption(tr("ДЕЙСТВИЯ"), actionsCard));
    auto* apply = button(tr("Применить"), actionsCard, QStringLiteral("berryPrimary"));
    apply->setMinimumHeight(40);
    connect(apply, &QPushButton::clicked, this, [this] { applyTweaks(tr("Настройки")); });
    auto* reset = button(tr("Сбросить конфиги"), actionsCard);
    reset->setMinimumHeight(40);
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
    layout->setContentsMargins(32, 26, 32, 26);
    layout->setSpacing(16);

    layout->addWidget(pageHeader(content, tr("ДИАГНОСТИКА"), tr("Состояние"),
                                 tr("Всё, что нужно, чтобы понять, почему игра не запускается.")));

    auto* status = card(content);
    auto* grid = new QGridLayout(status);
    grid->setContentsMargins(22, 20, 22, 20);
    grid->setHorizontalSpacing(24);
    grid->setVerticalSpacing(12);
    const QStringList keys{ tr("Лаунчер"), tr("Экземпляр"), tr("Java"), tr("Моды"),
                            tr("Сборка"), tr("Память"), tr("Диск") };
    for (int i = 0; i < keys.size(); ++i) {
        auto* key = caption(keys.at(i), status);
        key->setMinimumWidth(120);
        auto* value = new QLabel(QStringLiteral("—"), status);
        value->setObjectName(QStringLiteral("berryMono"));
        value->setWordWrap(true);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_diagValues.append(value);
        grid->addWidget(key, i, 0, Qt::AlignTop);
        grid->addWidget(value, i, 1);
    }
    grid->setColumnStretch(1, 1);
    layout->addWidget(status);

    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(10);
    auto* refresh = button(tr("Обновить"), content);
    connect(refresh, &QPushButton::clicked, this, &Panel::refreshDiagnostics);
    auto* folder = button(tr("Открыть папку"), content);
    connect(folder, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(Config::gameRoot()));
    });
    auto* copy = button(tr("Скопировать отчёт"), content, QStringLiteral("berryPrimary"));
    connect(copy, &QPushButton::clicked, this, [this] {
        const QStringList keys{ tr("Лаунчер"), tr("Экземпляр"), tr("Java"), tr("Моды"),
                                tr("Сборка"), tr("Память"), tr("Диск") };
        QStringList report;
        report << QStringLiteral("BerryTrip %1 · Minecraft 1.21.1 · NeoForge 21.1.267").arg(Config::version());
        for (int i = 0; i < keys.size() && i < m_diagValues.size(); ++i)
            report << QStringLiteral("%1: %2").arg(keys.at(i), m_diagValues.at(i)->text());
        QApplication::clipboard()->setText(report.join(QLatin1Char('\n')));
        m_aboutStatus->setText(tr("Отчёт скопирован — его можно отправить в чат."));
    });
    buttons->addWidget(refresh);
    buttons->addWidget(folder);
    buttons->addStretch(1);
    buttons->addWidget(copy);
    layout->addLayout(buttons);
    layout->addStretch(1);

    return page(content);
}

QWidget* Panel::buildAbout()
{
    auto* content = new QWidget;
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(32, 26, 32, 26);
    layout->setSpacing(16);

    layout->addWidget(pageHeader(content, tr("О ПРОГРАММЕ"), tr("BerryTrip"),
                                 tr("Неофициальный форк Prism Launcher для сборки BerryTrip.")));

    auto* head = new QFrame;
    head->setObjectName(QStringLiteral("berryHero"));
    auto* headRow = new QHBoxLayout(head);
    headRow->setContentsMargins(24, 22, 24, 22);
    headRow->setSpacing(20);
    auto* logo = new QLabel(head);
    logo->setPixmap(QPixmap(QStringLiteral(":/berry/berry.png"))
                        .scaled(72, 72, Qt::KeepAspectRatio, Qt::FastTransformation));
    headRow->addWidget(logo);
    auto* text = new QVBoxLayout;
    text->setSpacing(6);
    auto* title = new QLabel(QStringLiteral("BerryTrip %1").arg(Config::version()), head);
    title->setObjectName(QStringLiteral("berryTitle"));
    auto* sub = new QLabel(QStringLiteral("MINECRAFT 1.21.1 · NEOFORGE 21.1.267"), head);
    sub->setObjectName(QStringLiteral("berryCaption"));
    text->addWidget(title);
    text->addWidget(sub);
    headRow->addLayout(text, 1);
    layout->addWidget(head);

    auto* links = card(content);
    auto* linksLayout = new QVBoxLayout(links);
    linksLayout->setContentsMargins(20, 18, 20, 18);
    linksLayout->setSpacing(2);
    linksLayout->addWidget(caption(tr("ССЫЛКИ"), links));
    linksLayout->addSpacing(6);

    const auto addLink = [&](const QString& text, const QString& url) {
        auto* b = button(QStringLiteral(">  %1").arg(text), links, QStringLiteral("berryGhost"));
        b->setMinimumHeight(32);
        b->setStyleSheet(QStringLiteral("text-align: left; padding-left: 0px;"));
        connect(b, &QPushButton::clicked, this, [url] { QDesktopServices::openUrl(QUrl(url)); });
        linksLayout->addWidget(b);
    };
    addLink(tr("Сайт сборки"), QString::fromLatin1(SiteUrl));
    addLink(tr("Telegram-бот"), QString::fromLatin1(TelegramUrl));
    addLink(tr("Релизы лаунчера"), QString::fromLatin1(ReleasesUrl));
    addLink(tr("Исходники (GPL-3.0)"), QString::fromLatin1(SourcesUrl));
    addLink(tr("Prism Launcher — оригинал (GPL-3.0)"), QString::fromLatin1(UpstreamUrl));
    layout->addWidget(links);

    auto* legal = card(content);
    auto* legalLayout = new QVBoxLayout(legal);
    legalLayout->setContentsMargins(20, 18, 20, 18);
    legalLayout->setSpacing(6);
    legalLayout->addWidget(caption(tr("ЛИЦЕНЗИЯ"), legal));
    auto* legalText = new QLabel(
        tr("Распространяется по GNU GPL v3. Исходный код этой сборки: github.com/evilvalue/prism-berry, "
           "ветка berry, тег v%1 — он соответствует полученному файлу. © 2022-2026 Prism Launcher "
           "Contributors, © 2021-2022 PolyMC Contributors, © 2012-2021 MultiMC Contributors, © 2026 BerryTrip.\n\n"
           "Minecraft — торговая марка Mojang Studios. Проект не связан с Mojang и Microsoft.")
            .arg(Config::version()),
        legal);
    legalText->setObjectName(QStringLiteral("berryDim"));
    legalText->setWordWrap(true);
    legalLayout->addWidget(legalText);
    layout->addWidget(legal);

    auto* updateCard = card(content);
    auto* updateLayout = new QVBoxLayout(updateCard);
    updateLayout->setContentsMargins(20, 18, 20, 18);
    updateLayout->setSpacing(8);
    updateLayout->addWidget(caption(tr("ОБНОВЛЕНИЕ"), updateCard));
    auto* updateButtons = new QHBoxLayout;
    updateButtons->setSpacing(8);
    auto* check = button(tr("Проверить обновление"), updateCard, QStringLiteral("berryPrimary"));
    connect(check, &QPushButton::clicked, this, &Panel::checkUpdates);
    updateButtons->addWidget(check);
    m_aboutUpdate = button(tr("Скачать обновление"), updateCard);
    m_aboutUpdate->hide();
    connect(m_aboutUpdate, &QPushButton::clicked, this, &Panel::installUpdate);
    updateButtons->addWidget(m_aboutUpdate);
    updateButtons->addStretch(1);
    updateLayout->addLayout(updateButtons);
    m_aboutStatus = new QLabel(tr("Сборка обновляется сама при запуске."), updateCard);
    m_aboutStatus->setObjectName(QStringLiteral("berryDim"));
    m_aboutStatus->setWordWrap(true);
    updateLayout->addWidget(m_aboutStatus);
    layout->addWidget(updateCard);
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
                m_syncFinished = true;
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
    m_motd->setText(tr("Проверяю сервер…"));
    m_players->setText(tr("Игроки: —"));
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
    window->menuBar()->hide();
    window->statusBar()->hide();
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

    window->installEventFilter(new OverlayFilter(panel, window));
    qApp->installEventFilter(new PopupFilter(panel, qApp));
    window->setFixedSize(1240, 760);
}

}  // namespace berry
