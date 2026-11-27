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
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenuBar>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
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

#include "Application.h"
#include "LaunchMode.h"
#include "berry/BerryAuth.h"
#include "berry/BerryConfig.h"
#include "berry/BerryInstance.h"
#include "berry/BerryPackSync.h"
#include "berry/BerryPing.h"
#include "berry/BerryTheme.h"
#include "berry/BerryTweaks.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/launch/MinecraftTarget.h"
#include "settings/SettingsObject.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace berry {

namespace {

constexpr auto SiteUrl = "https://evilvalue.github.io/berry/";
constexpr auto TelegramUrl = "https://t.me/killberries";
constexpr auto ReleasesUrl = "https://github.com/evilvalue/prism-berry/releases/latest";
constexpr auto SourcesUrl = "https://github.com/evilvalue/prism-berry";

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
    return label;
}

QPushButton* button(const QString& text, QWidget* parent, const QString& objectName = QString())
{
    auto* b = new QPushButton(text, parent);
    b->setAutoDefault(false);
    if (!objectName.isEmpty())
        b->setObjectName(objectName);
    return b;
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

Panel::Panel(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("berryRoot"));
    theme::apply(this);

    m_auth = new TelegramAuth(this);
    m_ping = new ServerPing(this);

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---------------------------------------------------------------- sidebar
    auto* side = new QWidget(this);
    side->setObjectName(QStringLiteral("berrySide"));
    side->setFixedWidth(228);
    side->setStyleSheet(QStringLiteral("#berrySide { background: #12151B; border-right: 2px solid #282E3A; }"));
    auto* sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(18, 22, 18, 18);
    sideLayout->setSpacing(6);

    auto* logo = new QLabel(side);
    logo->setPixmap(QPixmap(QStringLiteral(":/berry/berry.png"))
                        .scaled(56, 56, Qt::KeepAspectRatio, Qt::FastTransformation));
    sideLayout->addWidget(logo);

    auto* name = new QLabel(QStringLiteral("BerryTrip"), side);
    name->setObjectName(QStringLiteral("berryH1"));
    sideLayout->addWidget(name);

    auto* tagline = caption(QStringLiteral("ЛАУНЧЕР СБОРКИ"), side);
    sideLayout->addWidget(tagline);
    sideLayout->addSpacing(16);

    m_pages = new QStackedWidget(this);

    const QStringList titles{ tr("Главное"), tr("Аккаунт"), tr("Настройки"), tr("Диагностика"), tr("О программе") };
    auto* group = new QButtonGroup(this);
    group->setExclusive(true);
    for (int i = 0; i < titles.size(); ++i) {
        auto* nav = button(titles.at(i), side, QStringLiteral("berryNav"));
        nav->setCheckable(true);
        nav->setChecked(i == 0);
        group->addButton(nav, i);
        sideLayout->addWidget(nav);
    }

    sideLayout->addStretch(1);
    auto* version = caption(QStringLiteral("ВЕРСИЯ %1").arg(Config::version()), side);
    sideLayout->addWidget(version);
    auto* siteButton = button(tr("Сайт сборки"), side, QStringLiteral("berryGhost"));
    connect(siteButton, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl(SiteUrl)); });
    sideLayout->addWidget(siteButton);

    // ---------------------------------------------------------------- pages
    m_pages->addWidget(buildHome());
    m_pages->addWidget(buildAccount());
    m_pages->addWidget(buildSettings());
    m_pages->addWidget(buildDiagnostics());
    m_pages->addWidget(buildAbout());

    connect(group, &QButtonGroup::idClicked, this, [this](int index) {
        m_pages->setCurrentIndex(index);
        if (index == 1)
            refreshAuth();
        if (index == 3)
            refreshDiagnostics();
    });

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

    connect(m_instance, &BaseInstance::runningStatusChanged, this, [this] { refreshLaunch(); });

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(2000);
    connect(m_pollTimer, &QTimer::timeout, m_auth, &TelegramAuth::poll);

    connect(m_auth, &TelegramAuth::message, this, [this](const QString& text, bool failure) {
        m_authStatus->setText(text);
        m_authStatus->setObjectName(failure ? QStringLiteral("berryErr") : QStringLiteral("berryDim"));
        m_authStatus->setStyleSheet(failure ? QStringLiteral("color: #FF6E7B;") : QStringLiteral("color: #747E90;"));
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
            m_pingLabel->setText(QStringLiteral("Ping: %1 мс").arg(status.ping));
            m_pingLabel->setStyleSheet(status.ping < 120 ? QStringLiteral("color:#4BE39F;")
                                                         : status.ping < 300 ? QStringLiteral("color:#F5B942;")
                                                                             : QStringLiteral("color:#FF6E7B;"));
        } else {
            m_motd->setText(tr("Сервер недоступен"));
            m_players->setText(status.error);
            m_pingLabel->setText(QStringLiteral("Ping: —"));
            m_pingLabel->setStyleSheet(QStringLiteral("color:#FF6E7B;"));
        }
    });

    Tweaks::applyAll();

    refreshAuth();
    refreshLaunch();
    refreshDiagnostics();
    startSync();
    startPing();
}

QWidget* Panel::page(QWidget* content)
{
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet(QStringLiteral("QScrollArea { background: #0E1015; } QScrollArea > QWidget > QWidget { background: #0E1015; }"));
    scroll->setWidget(content);
    return scroll;
}

QWidget* Panel::buildHome()
{
    auto* content = new QWidget;
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(26, 24, 26, 24);
    layout->setSpacing(14);

    // Hero
    auto* hero = new QFrame;
    hero->setObjectName(QStringLiteral("berryHero"));
    auto* heroRow = new QHBoxLayout(hero);
    heroRow->setContentsMargins(22, 20, 22, 20);
    heroRow->setSpacing(18);

    auto* logo = new QLabel(hero);
    logo->setPixmap(QPixmap(QStringLiteral(":/berry/berry.png"))
                        .scaled(72, 72, Qt::KeepAspectRatio, Qt::FastTransformation));
    heroRow->addWidget(logo);

    auto* text = new QVBoxLayout;
    text->setSpacing(4);
    auto* title = new QLabel(tr("BerryTrip"), hero);
    title->setObjectName(QStringLiteral("berryTitle"));
    auto* subtitle = new QLabel(tr("Minecraft 1.21.1 · NeoForge 21.1.267"), hero);
    subtitle->setObjectName(QStringLiteral("berryMuted"));
    m_chip = new QLabel(tr("ПРОВЕРКА СБОРКИ"), hero);
    m_chip->setObjectName(QStringLiteral("berryWarn"));
    text->addWidget(title);
    text->addWidget(subtitle);
    text->addWidget(m_chip);
    heroRow->addLayout(text, 1);
    layout->addWidget(hero);

    // Server
    auto* server = card(content);
    auto* serverLayout = new QVBoxLayout(server);
    serverLayout->setContentsMargins(18, 16, 18, 16);
    serverLayout->setSpacing(8);
    serverLayout->addWidget(caption(tr("СЕРВЕР %1").arg(Config::serverAddress()), server));
    m_motd = new QLabel(tr("Проверяю сервер…"), server);
    m_motd->setObjectName(QStringLiteral("berryH1"));
    m_motd->setWordWrap(true);
    m_players = new QLabel(tr("Игроки: —"), server);
    m_players->setObjectName(QStringLiteral("berryMuted"));
    m_pingLabel = new QLabel(tr("Ping: —"), server);
    m_pingLabel->setObjectName(QStringLiteral("berryDim"));
    serverLayout->addWidget(m_motd);
    serverLayout->addWidget(m_players);
    serverLayout->addWidget(m_pingLabel);

    auto* serverButtons = new QHBoxLayout;
    auto* refresh = button(tr("Обновить пинг"), server);
    connect(refresh, &QPushButton::clicked, this, &Panel::startPing);
    auto* telegram = button(tr("Telegram-канал"), server);
    connect(telegram, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl(TelegramUrl)); });
    serverButtons->addWidget(refresh);
    serverButtons->addWidget(telegram);
    serverButtons->addStretch(1);
    serverLayout->addLayout(serverButtons);
    layout->addWidget(server);

    // Account strip
    auto* account = card(content);
    auto* accountRow = new QHBoxLayout(account);
    accountRow->setContentsMargins(18, 12, 18, 12);
    auto* accountText = new QVBoxLayout;
    accountText->setSpacing(2);
    accountText->addWidget(caption(tr("АККАУНТ"), account));
    m_homeAccount = new QLabel(tr("Вход не выполнен"), account);
    accountText->addWidget(m_homeAccount);
    accountRow->addLayout(accountText, 1);
    auto* openAccount = button(tr("Аккаунт"), account);
    connect(openAccount, &QPushButton::clicked, this, [this] {
        m_pages->setCurrentIndex(1);
        refreshAuth();
    });
    accountRow->addWidget(openAccount);
    layout->addWidget(account);

    // Update + Play
    auto* update = card(content);
    auto* updateLayout = new QVBoxLayout(update);
    updateLayout->setContentsMargins(18, 16, 18, 16);
    updateLayout->setSpacing(10);
    updateLayout->addWidget(caption(tr("СБОРКА"), update));
    m_progress = new QProgressBar(update);
    m_progress->setTextVisible(false);
    m_status = new QLabel(tr("Подготовка…"), update);
    m_status->setObjectName(QStringLiteral("berryDim"));
    m_status->setWordWrap(true);
    updateLayout->addWidget(m_progress);
    updateLayout->addWidget(m_status);

    auto* actions = new QHBoxLayout;
    m_retry = button(tr("Повторить обновление"), update);
    m_retry->hide();
    connect(m_retry, &QPushButton::clicked, this, &Panel::startSync);
    m_play = button(tr("Играть"), update, QStringLiteral("berryPrimary"));
    m_play->setStyleSheet(QStringLiteral("font-size: 18px; padding: 14px 30px;"));
    m_play->setMinimumSize(220, 54);
    connect(m_play, &QPushButton::clicked, this, &Panel::onPlay);
    actions->addWidget(m_retry);
    actions->addStretch(1);
    actions->addWidget(m_play);
    updateLayout->addLayout(actions);
    layout->addWidget(update);
    layout->addStretch(1);

    return page(content);
}

QWidget* Panel::buildAccount()
{
    auto* content = new QWidget;
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(26, 24, 26, 24);
    layout->setSpacing(14);

    auto* head = new QLabel(tr("Аккаунт"), content);
    head->setObjectName(QStringLiteral("berryTitle"));
    layout->addWidget(head);
    auto* sub = new QLabel(tr("Вход только через Telegram-бота — Microsoft-аккаунт не нужен."), content);
    sub->setObjectName(QStringLiteral("berryMuted"));
    layout->addWidget(sub);

    auto* loginCard = card(content);
    auto* loginLayout = new QVBoxLayout(loginCard);
    loginLayout->setContentsMargins(18, 16, 18, 16);
    loginLayout->setSpacing(10);
    loginLayout->addWidget(caption(tr("НИК В MINECRAFT"), loginCard));

    auto* nickRow = new QHBoxLayout;
    m_nick = new QLineEdit(loginCard);
    m_nick->setPlaceholderText(tr("3–16 символов, латиница, цифры и _"));
    nickRow->addWidget(m_nick, 1);
    m_login = button(tr("Войти через Telegram"), loginCard, QStringLiteral("berryPrimary"));
    connect(m_login, &QPushButton::clicked, this, &Panel::onAuthClicked);
    nickRow->addWidget(m_login);
    loginLayout->addLayout(nickRow);

    m_authStatus = new QLabel(tr("Введи ник и подтверди вход в боте."), loginCard);
    m_authStatus->setObjectName(QStringLiteral("berryDim"));
    m_authStatus->setWordWrap(true);
    loginLayout->addWidget(m_authStatus);

    auto* secondary = new QHBoxLayout;
    m_openTelegram = button(tr("Открыть Telegram"), loginCard);
    connect(m_openTelegram, &QPushButton::clicked, this, [this] {
        m_auth->openPendingLink();
        m_authStatus->setText(tr("Telegram открыт — нажми Start в боте."));
    });
    m_logout = button(tr("Выйти"), loginCard, QStringLiteral("berryGhost"));
    connect(m_logout, &QPushButton::clicked, this, [this] {
        m_auth->logout();
        refreshAuth();
    });
    secondary->addWidget(m_openTelegram);
    secondary->addWidget(m_logout);
    secondary->addStretch(1);
    loginLayout->addLayout(secondary);
    layout->addWidget(loginCard);

    auto* help = card(content);
    auto* helpLayout = new QVBoxLayout(help);
    helpLayout->setContentsMargins(18, 16, 18, 16);
    helpLayout->setSpacing(6);
    helpLayout->addWidget(caption(tr("КАК ЭТО РАБОТАЕТ"), help));
    auto* steps = new QLabel(
        tr("1. Введи ник, под которым играешь.\n"
           "2. Лаунчер откроет бота и покажет «Ждём Start».\n"
           "3. Нажми Start в Telegram — ник попадёт в вайтлист.\n"
           "4. Токен сохранится в сборке, и «Играть» разблокируется."),
        help);
    steps->setObjectName(QStringLiteral("berryMuted"));
    steps->setWordWrap(true);
    helpLayout->addWidget(steps);
    layout->addWidget(help);
    layout->addStretch(1);

    return page(content);
}

QWidget* Panel::buildSettings()
{
    auto* content = new QWidget;
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(26, 24, 26, 24);
    layout->setSpacing(14);

    auto* head = new QLabel(tr("Настройки"), content);
    head->setObjectName(QStringLiteral("berryTitle"));
    layout->addWidget(head);
    auto* sub = new QLabel(tr("Моды, графика, память и Java. На сервер эти настройки не влияют."), content);
    sub->setObjectName(QStringLiteral("berryMuted"));
    layout->addWidget(sub);

    // Mods
    auto* mods = card(content);
    auto* modsLayout = new QVBoxLayout(mods);
    modsLayout->setContentsMargins(18, 16, 18, 16);
    modsLayout->setSpacing(8);
    modsLayout->addWidget(caption(tr("МОДЫ"), mods));
    for (const Tweaks::Mod& mod : Tweaks::mods()) {
        auto* check = new QCheckBox(mod.label, mods);
        check->setToolTip(QStringLiteral("%1 — %2. Выключение освобождает память.").arg(mod.label, mod.hint));
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
    auto* locked = new QLabel(tr("Всегда включены: JourneyMap · Emotecraft · Immersive Paintings · Not Enough Animations"),
                              mods);
    locked->setObjectName(QStringLiteral("berryDim"));
    locked->setWordWrap(true);
    modsLayout->addWidget(locked);
    layout->addWidget(mods);

    // Graphics
    auto* graphics = card(content);
    auto* graphicsLayout = new QVBoxLayout(graphics);
    graphicsLayout->setContentsMargins(18, 16, 18, 16);
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
    layout->addWidget(graphics);

    // Memory
    auto* memoryCard = card(content);
    auto* memoryLayout = new QVBoxLayout(memoryCard);
    memoryLayout->setContentsMargins(18, 16, 18, 16);
    memoryLayout->setSpacing(8);
    memoryLayout->addWidget(caption(tr("ПАМЯТЬ (ВСЕГО ОЗУ: %1 ГБ)").arg(totalRamGb()), memoryCard));
    auto* memoryRow = new QHBoxLayout;
    auto* minus = button(QStringLiteral("−"), memoryCard);
    m_memory = new QSpinBox(memoryCard);
    m_memory->setRange(2, qMax(4, totalRamGb()));
    m_memory->setSuffix(tr(" ГБ"));
    m_memory->setFixedWidth(120);
    auto* plus = button(QStringLiteral("+"), memoryCard);
    connect(minus, &QPushButton::clicked, this, [this] { m_memory->setValue(m_memory->value() - 1); });
    connect(plus, &QPushButton::clicked, this, [this] { m_memory->setValue(m_memory->value() + 1); });
    connect(m_memory, &QSpinBox::valueChanged, this, &Panel::onMemoryChanged);
    memoryRow->addWidget(minus);
    memoryRow->addWidget(m_memory);
    memoryRow->addWidget(plus);
    memoryRow->addStretch(1);
    memoryLayout->addLayout(memoryRow);
    layout->addWidget(memoryCard);

    // Java
    auto* javaCard = card(content);
    auto* javaLayout = new QVBoxLayout(javaCard);
    javaLayout->setContentsMargins(18, 16, 18, 16);
    javaLayout->setSpacing(8);
    javaLayout->addWidget(caption(tr("JAVA"), javaCard));
    auto* javaRow = new QHBoxLayout;
    m_javaPath = new QLineEdit(javaCard);
    m_javaPath->setPlaceholderText(tr("Автоматически (Java из Prism)"));
    connect(m_javaPath, &QLineEdit::editingFinished, this, &Panel::onJavaEdited);
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
    javaRow->addWidget(m_javaPath, 1);
    javaRow->addWidget(browse);
    javaRow->addWidget(autoJava);
    javaLayout->addLayout(javaRow);
    layout->addWidget(javaCard);

    // Actions
    auto* actions = new QHBoxLayout;
    auto* reset = button(tr("Сбросить конфиги"), content);
    connect(reset, &QPushButton::clicked, this, [this] {
        const QString backup = Tweaks::resetConfigs();
        if (backup.isEmpty()) {
            m_settingsStatus->setText(tr("Не удалось сбросить конфиги."));
            return;
        }
        m_settingsStatus->setText(tr("Конфиги сброшены. Копия: %1").arg(backup));
        startSync();
    });
    auto* apply = button(tr("Применить"), content, QStringLiteral("berryPrimary"));
    connect(apply, &QPushButton::clicked, this, [this] { applyTweaks(tr("Настройки")); });
    actions->addWidget(reset);
    actions->addStretch(1);
    actions->addWidget(apply);
    layout->addLayout(actions);

    m_settingsStatus = new QLabel(tr("Изменения применяются сразу."), content);
    m_settingsStatus->setObjectName(QStringLiteral("berryDim"));
    m_settingsStatus->setWordWrap(true);
    layout->addWidget(m_settingsStatus);
    layout->addStretch(1);

    return page(content);
}

QWidget* Panel::buildDiagnostics()
{
    auto* content = new QWidget;
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(26, 24, 26, 24);
    layout->setSpacing(14);

    auto* head = new QLabel(tr("Диагностика"), content);
    head->setObjectName(QStringLiteral("berryTitle"));
    layout->addWidget(head);

    auto* status = card(content);
    auto* grid = new QGridLayout(status);
    grid->setContentsMargins(18, 16, 18, 16);
    grid->setHorizontalSpacing(18);
    grid->setVerticalSpacing(7);
    const QStringList keys{ tr("Лаунчер"), tr("Экземпляр"), tr("Java"), tr("Моды"),
                            tr("Сборка"), tr("Память"), tr("Диск") };
    for (int i = 0; i < keys.size(); ++i) {
        auto* key = new QLabel(keys.at(i), status);
        key->setObjectName(QStringLiteral("berryMuted"));
        auto* value = new QLabel(QStringLiteral("—"), status);
        value->setWordWrap(true);
        m_diagValues.append(value);
        grid->addWidget(key, i, 0);
        grid->addWidget(value, i, 1);
    }
    grid->setColumnStretch(1, 1);
    layout->addWidget(status);

    auto* buttons = new QHBoxLayout;
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
        report << QStringLiteral("BerryTrip %1 · BerryTrip pack · Minecraft 1.21.1 · NeoForge 21.1.267")
                      .arg(Config::version());
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
    layout->setContentsMargins(26, 24, 26, 24);
    layout->setSpacing(14);

    auto* head = new QFrame;
    head->setObjectName(QStringLiteral("berryHero"));
    auto* headRow = new QHBoxLayout(head);
    headRow->setContentsMargins(22, 20, 22, 20);
    auto* logo = new QLabel(head);
    logo->setPixmap(QPixmap(QStringLiteral(":/berry/berry.png"))
                        .scaled(64, 64, Qt::KeepAspectRatio, Qt::FastTransformation));
    headRow->addWidget(logo);
    auto* text = new QVBoxLayout;
    auto* title = new QLabel(QStringLiteral("BerryTrip %1").arg(Config::version()), head);
    title->setObjectName(QStringLiteral("berryH1"));
    auto* sub = new QLabel(tr("BerryTrip · Minecraft 1.21.1 · NeoForge 21.1.267"), head);
    sub->setObjectName(QStringLiteral("berryMuted"));
    text->addWidget(title);
    text->addWidget(sub);
    headRow->addLayout(text, 1);
    layout->addWidget(head);

    auto* links = card(content);
    auto* linksLayout = new QVBoxLayout(links);
    linksLayout->setContentsMargins(18, 16, 18, 16);
    linksLayout->setSpacing(8);
    linksLayout->addWidget(caption(tr("ССЫЛКИ"), links));

    const auto addLink = [&](const QString& text, const QString& url) {
        auto* b = button(text, links);
        connect(b, &QPushButton::clicked, this, [url] { QDesktopServices::openUrl(QUrl(url)); });
        linksLayout->addWidget(b);
    };
    addLink(tr("Сайт сборки"), QString::fromLatin1(SiteUrl));
    addLink(tr("Telegram-канал"), QString::fromLatin1(TelegramUrl));
    addLink(tr("Релизы лаунчера"), QString::fromLatin1(ReleasesUrl));
    addLink(tr("Исходники (GPL-3.0)"), QString::fromLatin1(SourcesUrl));
    layout->addWidget(links);

    auto* updateCard = card(content);
    auto* updateLayout = new QVBoxLayout(updateCard);
    updateLayout->setContentsMargins(18, 16, 18, 16);
    updateLayout->setSpacing(8);
    updateLayout->addWidget(caption(tr("ОБНОВЛЕНИЕ"), updateCard));
    auto* check = button(tr("Проверить обновление"), updateCard, QStringLiteral("berryPrimary"));
    connect(check, &QPushButton::clicked, this, &Panel::checkUpdates);
    updateLayout->addWidget(check);
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

void Panel::refreshAuth()
{
    const auto state = m_auth->tokenState();
    const bool pending = m_auth->pending();
    const QString nick = m_auth->tokenNick().isEmpty() ? m_auth->nick() : m_auth->tokenNick();

    if (state == TelegramAuth::TokenState::Ok) {
        m_homeAccount->setText(tr("%1 · вход выполнен").arg(nick));
        m_homeAccount->setStyleSheet(QStringLiteral("color:#4BE39F;"));
        m_login->setText(tr("Войти заново"));
        m_logout->setVisible(true);
    } else {
        m_homeAccount->setText(pending ? tr("Ожидает подтверждения в Telegram") : tr("Вход не выполнен"));
        m_homeAccount->setStyleSheet(pending ? QStringLiteral("color:#FFA36B;") : QStringLiteral("color:#A9B2C3;"));
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
        m_auth->openPendingLink();
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
    m_play->setText(running ? tr("Игра запущена") : tr("Играть"));
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
            m_status->setText(tr("Обновление: %1 (%2/%3)").arg(QFileInfo(file).fileName()).arg(done).arg(total));
        });
        connect(m_sync, &PackSync::finished, this, [this](bool ok, const QString& error) {
            m_progress->hide();
            m_syncFinished = true;
            m_retry->setVisible(!ok);
            m_status->setText(ok ? tr("Сборка готова") : tr("Не удалось обновить: %1").arg(error));
            if (ok)
                Tweaks::applyAll();
            refreshLaunch();
            refreshDiagnostics();
        });
    }

    m_progress->setMaximum(0);
    m_progress->setValue(0);
    m_progress->show();
    m_retry->hide();
    m_status->setText(tr("Проверка файлов сборки…"));
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
    m_aboutStatus->setText(tr("Проверяю…"));

    QNetworkRequest request(QUrl(QStringLiteral("https://api.github.com/repos/evilvalue/prism-berry/releases/latest")));
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("BerryTrip/") + Config::version());
    request.setTransferTimeout(10000);

    auto* reply = APPLICATION->network()->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const QByteArray body = reply->readAll();
        const bool failed = reply->error() != QNetworkReply::NoError;
        reply->deleteLater();
        if (failed) {
            m_aboutStatus->setText(tr("Проверка не удалась — открой «Релизы» вручную."));
            return;
        }
        const QString tag = QJsonDocument::fromJson(body).object().value(QStringLiteral("tag_name")).toString();
        if (tag.isEmpty()) {
            m_aboutStatus->setText(tr("Релизов пока нет."));
            return;
        }
        const QString latest = tag.startsWith(QLatin1Char('v')) ? tag.mid(1) : tag;
        if (latest == Config::version()) {
            m_aboutStatus->setText(tr("Установлена последняя версия: %1").arg(Config::version()));
            m_aboutStatus->setStyleSheet(QStringLiteral("color:#4BE39F;"));
        } else {
            m_aboutStatus->setText(tr("Доступна версия %1 — скачай её со страницы релизов.").arg(latest));
            m_aboutStatus->setStyleSheet(QStringLiteral("color:#FF5D86;"));
        }
    });
}

void Panel::onPlay()
{
    if (!m_instance || m_instance->isRunning())
        return;

    if (!signedIn()) {
        m_pages->setCurrentIndex(1);
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
    window->setMinimumSize(960, 680);
    window->resize(1180, 780);
}

}  // namespace berry
