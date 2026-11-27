// SPDX-License-Identifier: GPL-3.0-only
/*
 *  BerryTrip Launcher - single server build of Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "berry/BerryPanel.h"

#include <QDir>
#include <QDockWidget>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenuBar>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#include "Application.h"
#include "LaunchMode.h"
#include "berry/BerryAuth.h"
#include "berry/BerryConfig.h"
#include "berry/BerryInstance.h"
#include "berry/BerryPackSync.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/launch/MinecraftTarget.h"
#include "settings/SettingsObject.h"

namespace berry {

namespace {

/// Keeps the panel covering the window's client area while the window resizes.
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

QFrame* makeCard(QWidget* parent)
{
    auto* card = new QFrame(parent);
    card->setObjectName(QStringLiteral("berryCard"));
    card->setFrameShape(QFrame::NoFrame);
    return card;
}

}  // namespace

Panel::Panel(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("berryPanel"));
    setAttribute(Qt::WA_StyledBackground, true);

    m_auth = new TelegramAuth(this);
    buildUi();

    QString error;
    m_instance = InstanceSetup::ensure(&error);
    if (!m_instance) {
        m_status->setText(error.isEmpty() ? tr("Не удалось подготовить сборку") : error);
        m_play->setEnabled(false);
        m_retry->hide();
        return;
    }

    loadSettings();
    connect(m_instance, &BaseInstance::runningStatusChanged, this, [this] { refreshLaunchButton(); });

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(2000);
    connect(m_pollTimer, &QTimer::timeout, m_auth, &TelegramAuth::poll);

    connect(m_auth, &TelegramAuth::message, this, [this](const QString& text, bool failure) {
        m_authStatus->setText(text);
        m_authStatus->setStyleSheet(failure ? QStringLiteral("color: #d24b4b;") : QStringLiteral("color: palette(placeholder-text);"));
        refreshAuth();
    });
    connect(m_auth, &TelegramAuth::authenticated, this, [this](const QString& nick) {
        TelegramAuth::useOfflineAccount(nick);
        refreshAuth();
    });

    refreshAuth();
    refreshLaunchButton();
    startSync();
}

void Panel::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(32, 28, 32, 28);
    root->setSpacing(18);

    auto* title = new QLabel(tr("BerryTrip"), this);
    title->setObjectName(QStringLiteral("berryTitle"));

    auto* subtitle = new QLabel(tr("Сервер %1").arg(Config::serverAddress()), this);
    subtitle->setObjectName(QStringLiteral("berrySubtitle"));

    auto* header = new QVBoxLayout();
    header->setSpacing(2);
    header->addWidget(title);
    header->addWidget(subtitle);
    root->addLayout(header);

    auto* card = makeCard(this);
    auto* form = new QVBoxLayout(card);
    form->setContentsMargins(20, 20, 20, 20);
    form->setSpacing(14);

    // Account: Telegram only, no Microsoft accounts anywhere in this build.
    {
        auto* row = new QHBoxLayout();
        auto* caption = new QLabel(tr("Аккаунт"), card);
        caption->setMinimumWidth(90);
        m_accountLabel = new QLabel(card);
        m_accountLabel->setObjectName(QStringLiteral("berryAccount"));
        m_authButton = new QPushButton(tr("Войти через Telegram"), card);
        m_authButton->setAutoDefault(false);
        connect(m_authButton, &QPushButton::clicked, this, &Panel::onAuthClicked);
        row->addWidget(caption);
        row->addWidget(m_accountLabel, 1);
        row->addWidget(m_authButton);
        form->addLayout(row);

        m_authStatus = new QLabel(card);
        m_authStatus->setObjectName(QStringLiteral("berryAuthStatus"));
        m_authStatus->setWordWrap(true);
        m_authStatus->setContentsMargins(98, 0, 0, 0);
        form->addWidget(m_authStatus);
    }

    // Memory
    {
        auto* row = new QHBoxLayout();
        auto* caption = new QLabel(tr("Память"), card);
        caption->setMinimumWidth(90);
        m_memory = new QSpinBox(card);
        m_memory->setRange(2, 32);
        m_memory->setSuffix(tr(" ГБ"));
        m_memory->setFixedWidth(110);
        connect(m_memory, &QSpinBox::valueChanged, this, &Panel::onMemoryChanged);
        row->addWidget(caption);
        row->addWidget(m_memory);
        row->addStretch(1);
        form->addLayout(row);
    }

    // Java
    {
        auto* row = new QHBoxLayout();
        auto* caption = new QLabel(tr("Java"), card);
        caption->setMinimumWidth(90);
        m_javaPath = new QLineEdit(card);
        m_javaPath->setPlaceholderText(tr("Автоматически"));
        connect(m_javaPath, &QLineEdit::editingFinished, this, &Panel::onJavaEdited);
        auto* browse = new QPushButton(tr("Обзор…"), card);
        browse->setAutoDefault(false);
        connect(browse, &QPushButton::clicked, this, &Panel::onBrowseJava);
        row->addWidget(caption);
        row->addWidget(m_javaPath, 1);
        row->addWidget(browse);
        form->addLayout(row);
    }

    root->addWidget(card);
    root->addStretch(1);

    // Update state
    m_progress = new QProgressBar(this);
    m_progress->setTextVisible(false);
    m_progress->setFixedHeight(6);
    m_status = new QLabel(tr("Подготовка…"), this);
    m_status->setObjectName(QStringLiteral("berryStatus"));
    m_status->setWordWrap(true);
    root->addWidget(m_progress);
    root->addWidget(m_status);

    auto* actions = new QHBoxLayout();
    m_retry = new QPushButton(tr("Повторить обновление"), this);
    m_retry->setAutoDefault(false);
    m_retry->hide();
    connect(m_retry, &QPushButton::clicked, this, &Panel::startSync);

    m_play = new QPushButton(tr("Играть"), this);
    m_play->setObjectName(QStringLiteral("berryPlay"));
    m_play->setMinimumHeight(52);
    m_play->setMinimumWidth(220);
    m_play->setDefault(true);
    connect(m_play, &QPushButton::clicked, this, &Panel::onPlayClicked);

    actions->addStretch(1);
    actions->addWidget(m_retry);
    actions->addWidget(m_play);
    root->addLayout(actions);

    setStyleSheet(QStringLiteral(R"(
#berryPanel { background: palette(window); }
#berryTitle { font-size: 26px; font-weight: 600; }
#berrySubtitle { color: palette(placeholder-text); font-size: 13px; }
#berryStatus { color: palette(placeholder-text); font-size: 12px; }
#berryAuthStatus { color: palette(placeholder-text); font-size: 12px; }
#berryAccount { font-weight: 600; }
#berryCard { background: palette(base); border: 1px solid palette(mid); border-radius: 10px; }
#berryPlay { font-size: 16px; font-weight: 600; padding: 10px 28px; }
)"));
}

void Panel::loadSettings()
{
    auto* settings = m_instance->settings();

    const int megabytes = settings->get(QStringLiteral("MaxMemAlloc")).toInt();
    QSignalBlocker blocker(m_memory);
    m_memory->setValue(qBound(2, (megabytes > 0 ? megabytes : 8192) / 1024, 32));

    if (settings->get(QStringLiteral("OverrideJavaLocation")).toBool())
        m_javaPath->setText(QDir::toNativeSeparators(settings->get(QStringLiteral("JavaPath")).toString()));
}

void Panel::onMemoryChanged(int gigabytes)
{
    if (!m_instance)
        return;

    auto* settings = m_instance->settings();
    settings->set(QStringLiteral("OverrideMemory"), true);
    settings->set(QStringLiteral("MaxMemAlloc"), gigabytes * 1024);
}

void Panel::onJavaEdited()
{
    if (!m_instance)
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

void Panel::onBrowseJava()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Выберите Java"), m_javaPath->text(),
                                                      QStringLiteral("javaw.exe (javaw.exe);;Все файлы (*)"));
    if (path.isEmpty())
        return;

    m_javaPath->setText(QDir::toNativeSeparators(path));
    onJavaEdited();
}

bool Panel::signedIn() const
{
    return m_auth->tokenState() == TelegramAuth::TokenState::Ok;
}

void Panel::refreshAuth()
{
    const auto state = m_auth->tokenState();
    const bool pending = m_auth->pending();

    if (state == TelegramAuth::TokenState::Ok) {
        const QString nick = m_auth->tokenNick().isEmpty() ? m_auth->nick() : m_auth->tokenNick();
        TelegramAuth::useOfflineAccount(nick);
        m_accountLabel->setText(nick);
        m_authButton->setText(tr("Выйти"));
    } else if (pending) {
        m_accountLabel->setText(m_auth->nick().isEmpty() ? tr("Ожидание Telegram") : m_auth->nick());
        m_authButton->setText(tr("Открыть Telegram"));
    } else {
        m_accountLabel->setText(tr("Вход не выполнен"));
        m_authButton->setText(tr("Войти через Telegram"));
        switch (state) {
            case TelegramAuth::TokenState::Expired:
                m_authStatus->setText(tr("Сессия истекла — войдите снова."));
                break;
            case TelegramAuth::TokenState::OtherNick:
                m_authStatus->setText(tr("Токен выдан другому нику — войдите под этим ником."));
                break;
            case TelegramAuth::TokenState::Broken:
                m_authStatus->setText(tr("Файл токена повреждён — войдите снова."));
                break;
            case TelegramAuth::TokenState::Ok:
            case TelegramAuth::TokenState::None:
                if (m_authStatus->text().isEmpty())
                    m_authStatus->setText(tr("Вход только через бота — Microsoft-аккаунт не нужен."));
                break;
        }
    }

    if (m_pollTimer) {
        if (pending && !m_pollTimer->isActive())
            m_pollTimer->start();
        else if (!pending && m_pollTimer->isActive())
            m_pollTimer->stop();
    }

    refreshLaunchButton();
}

void Panel::onAuthClicked()
{
    if (signedIn()) {
        m_auth->logout();
        refreshAuth();
        return;
    }

    if (m_auth->pending()) {
        m_auth->openPendingLink();
        refreshAuth();
        return;
    }

    bool accepted = false;
    const QString nick = QInputDialog::getText(this, tr("Вход через Telegram"), tr("Ник в Minecraft:"), QLineEdit::Normal,
                                               m_auth->nick(), &accepted)
                             .trimmed();
    if (!accepted || nick.isEmpty())
        return;

    m_authStatus->setText(tr("Запрашиваю ссылку у сервера…"));
    m_auth->start(nick);
}

void Panel::refreshLaunchButton()
{
    if (!m_instance) {
        m_play->setEnabled(false);
        return;
    }

    if (m_instance->isRunning()) {
        m_play->setText(tr("Игра запущена"));
        m_play->setEnabled(false);
        return;
    }

    m_play->setText(tr("Играть"));
    m_play->setEnabled(m_syncFinished && signedIn());
}

void Panel::startSync()
{
    if (!m_instance || (m_sync && m_sync->isRunning()))
        return;

    m_syncFinished = false;
    refreshLaunchButton();

    if (!m_sync) {
        m_sync = new PackSync(this);
        connect(m_sync, &PackSync::progress, this, [this](int done, int total, const QString& file) {
            m_progress->setMaximum(total);
            m_progress->setValue(done);
            m_status->setText(tr("Обновление сборки: %1 (%2 из %3)").arg(QFileInfo(file).fileName()).arg(done).arg(total));
        });
        connect(m_sync, &PackSync::finished, this, [this](bool ok, const QString& error) {
            m_progress->hide();
            m_syncFinished = true;
            m_retry->setVisible(!ok);
            m_status->setText(ok ? tr("Сборка готова") : tr("Не удалось обновить сборку: %1").arg(error));
            refreshLaunchButton();
        });
    }

    m_progress->setMaximum(0);
    m_progress->setValue(0);
    m_progress->show();
    m_retry->hide();
    m_status->setText(tr("Проверка файлов сборки…"));
    m_sync->start();
}

void Panel::onPlayClicked()
{
    if (!m_instance || m_instance->isRunning())
        return;

    if (!signedIn()) {
        m_authStatus->setText(tr("Сначала войдите через Telegram-бота."));
        m_authStatus->setStyleSheet(QStringLiteral("color: #d24b4b;"));
        return;
    }

    auto target = std::make_shared<MinecraftTarget>(MinecraftTarget::parse(Config::serverAddress(), false));
    if (!APPLICATION->launch(m_instance, LaunchMode::Normal, target))
        m_status->setText(tr("Запуск не удался — проверьте аккаунт и Java"));
}

void installSoloUi(QMainWindow* window)
{
    window->menuBar()->hide();
    window->statusBar()->hide();
    for (auto* bar : window->findChildren<QToolBar*>())
        bar->hide();
    for (auto* dock : window->findChildren<QDockWidget*>())
        dock->hide();

    // Keep the instance manager alive (MainWindow still holds pointers into it)
    // but out of sight, and cover its area with the single-server panel.
    if (auto* central = window->centralWidget())
        central->hide();

    auto* panel = new Panel(window);
    panel->setGeometry(window->rect());
    panel->show();
    panel->raise();

    window->installEventFilter(new OverlayFilter(panel, window));
    window->setMinimumSize(560, 480);
    window->resize(720, 560);
}

}  // namespace berry
