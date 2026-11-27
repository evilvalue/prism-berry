// SPDX-License-Identifier: GPL-3.0-only
/*
 *  BerryTrip Launcher - single server build of Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "berry/BerryAuth.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QUrl>
#include <QUrlQuery>

#include "Application.h"
#include "berry/BerryConfig.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/auth/AccountList.h"
#include "minecraft/auth/MinecraftAccount.h"

namespace berry {

namespace {

QString settingsPath()
{
    return QDir(APPLICATION->dataRoot()).filePath(QStringLiteral("berry.ini"));
}

QByteArray stripBom(QByteArray data)
{
    if (data.startsWith("\xEF\xBB\xBF"))
        data.remove(0, 3);
    return data;
}

QByteArray base64UrlDecode(const QString& input)
{
    QString padded = input;
    padded.replace(QLatin1Char('-'), QLatin1Char('+')).replace(QLatin1Char('_'), QLatin1Char('/'));
    while (padded.size() % 4 != 0)
        padded.append(QLatin1Char('='));
    return QByteArray::fromBase64(padded.toLatin1());
}

}  // namespace

TelegramAuth::TelegramAuth(QObject* parent) : QObject(parent)
{
    QSettings store(settingsPath(), QSettings::IniFormat);
    m_pendingId = store.value(QStringLiteral("auth/request_id")).toString();
    m_pendingSecret = store.value(QStringLiteral("auth/request_secret")).toString();
    m_pendingNick = store.value(QStringLiteral("auth/request_nick")).toString();
    m_pendingLink = store.value(QStringLiteral("auth/request_link")).toString();
    m_pendingUntil = store.value(QStringLiteral("auth/request_until"), qint64(0)).toLongLong();

    if (!m_pendingId.isEmpty() && QDateTime::currentMSecsSinceEpoch() >= m_pendingUntil)
        clearPending();
}

QString TelegramAuth::nick() const
{
    QSettings store(settingsPath(), QSettings::IniFormat);
    return store.value(QStringLiteral("auth/nick")).toString();
}

bool TelegramAuth::pending() const
{
    return !m_pendingId.isEmpty() && QDateTime::currentMSecsSinceEpoch() < m_pendingUntil;
}

QString TelegramAuth::pendingLink() const
{
    return m_pendingLink;
}

QString TelegramAuth::tokenFilePath() const
{
    const QString root = Config::gameRoot();
    if (root.isEmpty())
        return {};
    return QDir(root).filePath(QStringLiteral("config/berryauth-token.json"));
}

void TelegramAuth::start(const QString& nick)
{
    if (m_busy)
        return;

    const QString clean = nick.trimmed();
    static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9_]{3,16}$"));
    if (!valid.match(clean).hasMatch()) {
        emit message(tr("Ник: 3–16 символов, латиница, цифры и _."), true);
        return;
    }

    if (pending()) {
        emit message(tr("Вход уже начат — подтвердите его в Telegram."), false);
        openPendingLink();
        return;
    }

    QSettings store(settingsPath(), QSettings::IniFormat);
    store.setValue(QStringLiteral("auth/nick"), clean);
    requestLink(clean);
}

void TelegramAuth::requestLink(const QString& nick)
{
    m_busy = true;

    QNetworkRequest request(QUrl(Config::authApi() + QStringLiteral("/auth/link")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json; charset=utf-8"));
    request.setTransferTimeout(12000);

    auto* reply = APPLICATION->network()->post(request, QJsonDocument(QJsonObject{ { "nick", nick } }).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, nick] {
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = stripBom(reply->readAll());
        const QString netError = reply->error() == QNetworkReply::NoError ? QString() : reply->errorString();
        reply->deleteLater();
        m_busy = false;

        if (!netError.isEmpty() && code == 0) {
            emit message(tr("Сервер входа недоступен: %1").arg(netError), true);
            return;
        }

        const auto json = QJsonDocument::fromJson(body).object();
        if (code != 200 || !json.value(QStringLiteral("ok")).toBool()) {
            const QString reason = json.value(QStringLiteral("error")).toString(json.value(QStringLiteral("message")).toString());
            emit message(reason.isEmpty() ? tr("Сервер входа ответил ошибкой %1.").arg(code) : reason, true);
            return;
        }

        const QString id = json.value(QStringLiteral("id")).toString();
        const QString secret = json.value(QStringLiteral("secret")).toString();
        const QString link = json.value(QStringLiteral("link")).toString();
        const qint64 expires = static_cast<qint64>(json.value(QStringLiteral("expires")).toDouble());

        if (id.isEmpty() || secret.isEmpty() || !link.startsWith(QLatin1String("https://t.me/")) ||
            expires <= QDateTime::currentMSecsSinceEpoch()) {
            emit message(tr("Сервер выдал некорректную ссылку. Попробуйте ещё раз."), true);
            return;
        }

        m_pendingId = id;
        m_pendingSecret = secret;
        m_pendingNick = nick;
        m_pendingLink = link;
        m_pendingUntil = expires;

        QSettings store(settingsPath(), QSettings::IniFormat);
        store.setValue(QStringLiteral("auth/request_id"), id);
        store.setValue(QStringLiteral("auth/request_secret"), secret);
        store.setValue(QStringLiteral("auth/request_nick"), nick);
        store.setValue(QStringLiteral("auth/request_link"), link);
        store.setValue(QStringLiteral("auth/request_until"), expires);
        store.sync();

        openTelegramLink(link);
        emit message(tr("Telegram открыт — нажмите Start в боте."), false);
    });
}

void TelegramAuth::poll()
{
    if (m_busy)
        return;

    if (m_pendingId.isEmpty() || m_pendingSecret.isEmpty()) {
        emit message(tr("Ссылка входа не найдена."), true);
        return;
    }

    if (QDateTime::currentMSecsSinceEpoch() >= m_pendingUntil) {
        clearPending();
        emit message(tr("Ссылка истекла — начните вход заново."), true);
        return;
    }

    m_busy = true;

    QNetworkRequest request(QUrl(Config::authApi() + QStringLiteral("/auth/status")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json; charset=utf-8"));
    request.setTransferTimeout(12000);

    const QJsonObject body{ { "id", m_pendingId }, { "secret", m_pendingSecret } };
    auto* reply = APPLICATION->network()->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = stripBom(reply->readAll());
        const bool networkDown = reply->error() != QNetworkReply::NoError && code == 0;
        reply->deleteLater();
        m_busy = false;

        // A blink of the network must not kill a still valid link.
        if (networkDown)
            return;

        const auto json = QJsonDocument::fromJson(body).object();
        const QString nick = json.value(QStringLiteral("nick")).toString();
        const QString status = json.value(QStringLiteral("status")).toString();

        if (code == 404) {
            clearPending();
            emit message(tr("Ссылка не найдена. Начните вход заново."), true);
            return;
        }
        if (code != 200) {
            emit message(tr("Сервер входа ответил ошибкой %1.").arg(code), true);
            return;
        }

        if (status == QLatin1String("waiting")) {
            emit message(tr("Ждём Start в Telegram…"), false);
            return;
        }

        if (status == QLatin1String("verified")) {
            const QString token = json.value(QStringLiteral("token")).toString();
            if (token.isEmpty()) {
                emit message(tr("Ждём подтверждение в Telegram…"), false);
                return;
            }
            const QString who = nick.isEmpty() ? m_pendingNick : nick;
            if (!writeToken(who, token)) {
                emit message(tr("Не удалось записать токен в сборку."), true);
                return;
            }
            QSettings verified(settingsPath(), QSettings::IniFormat);
            verified.setValue(QStringLiteral("auth/nick"), who);
            clearPending();
            emit message(tr("Вход подтверждён: %1").arg(who), false);
            emit authenticated(who);
            return;
        }

        if (status == QLatin1String("expired")) {
            clearPending();
            emit message(tr("Ссылка истекла — начните вход заново."), true);
            return;
        }

        const QString reason = json.value(QStringLiteral("error")).toString(json.value(QStringLiteral("message")).toString());
        clearPending();
        emit message(reason.isEmpty() ? tr("Вход отклонён.") : reason, true);
    });
}

bool TelegramAuth::openPendingLink()
{
    if (!pending() || m_pendingLink.isEmpty())
        return false;
    openTelegramLink(m_pendingLink);
    return true;
}

bool TelegramAuth::openTelegramLink(const QString& link)
{
    const QUrl https(link);
    if (!https.isValid())
        return false;

#ifdef Q_OS_WIN
    const QString roaming = qEnvironmentVariable("APPDATA");
    const QString local = qEnvironmentVariable("LOCALAPPDATA");
    const bool desktop = QFileInfo::exists(roaming + QStringLiteral("/Telegram Desktop/tdata")) ||
                         QFileInfo::exists(local + QStringLiteral("/Programs/Telegram Desktop/Telegram.exe")) ||
                         QFileInfo::exists(QStringLiteral("C:/Program Files/Telegram Desktop/Telegram.exe")) ||
                         QFileInfo::exists(QStringLiteral("C:/Program Files (x86)/Telegram Desktop/Telegram.exe"));
#else
    const bool desktop = false;
#endif

    const QString host = https.host().toLower();
    if (desktop && (host == QLatin1String("t.me") || host == QLatin1String("telegram.me"))) {
        const QString bot = https.path().mid(1);
        const QUrlQuery query(https);
        if (!bot.isEmpty()) {
            QUrl deep(QStringLiteral("tg://resolve"));
            QUrlQuery deepQuery;
            deepQuery.addQueryItem(QStringLiteral("domain"), bot);
            const QString startToken = query.queryItemValue(QStringLiteral("start"));
            if (!startToken.isEmpty())
                deepQuery.addQueryItem(QStringLiteral("start"), startToken);
            deep.setQuery(deepQuery);
            if (QDesktopServices::openUrl(deep))
                return true;
        }
    }

    return QDesktopServices::openUrl(https);
}

void TelegramAuth::logout()
{
    const QString path = tokenFilePath();
    if (!path.isEmpty())
        QFile::remove(path);
    const QString legacy = QFileInfo(path).dir().filePath(QStringLiteral("berryauth-link.json"));
    if (QFile::exists(legacy))
        QFile::remove(legacy);

    clearPending();
    QSettings store(settingsPath(), QSettings::IniFormat);
    store.remove(QStringLiteral("auth/nick"));
    store.sync();

    emit message(tr("Вы вышли из аккаунта."), false);
}

bool TelegramAuth::writeToken(const QString& nick, const QString& token)
{
    const QString path = tokenFilePath();
    if (path.isEmpty())
        return false;

    QDir().mkpath(QFileInfo(path).absolutePath());

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(QJsonObject{ { "nick", nick }, { "token", token } }).toJson(QJsonDocument::Compact));
    return file.commit();
}

void TelegramAuth::clearPending()
{
    m_pendingId.clear();
    m_pendingSecret.clear();
    m_pendingNick.clear();
    m_pendingLink.clear();
    m_pendingUntil = 0;

    QSettings store(settingsPath(), QSettings::IniFormat);
    store.remove(QStringLiteral("auth/request_id"));
    store.remove(QStringLiteral("auth/request_secret"));
    store.remove(QStringLiteral("auth/request_nick"));
    store.remove(QStringLiteral("auth/request_link"));
    store.remove(QStringLiteral("auth/request_until"));
    store.sync();
}

TelegramAuth::TokenState TelegramAuth::tokenState() const
{
    const QString path = tokenFilePath();
    if (path.isEmpty())
        return TokenState::None;

    QFile file(path);
    if (!file.exists() || file.size() == 0)
        return TokenState::None;
    if (!file.open(QIODevice::ReadOnly))
        return TokenState::Broken;

    const auto json = QJsonDocument::fromJson(file.readAll()).object();
    const QString token = json.value(QStringLiteral("token")).toString();
    const int dot = token.indexOf(QLatin1Char('.'));
    if (dot <= 0)
        return TokenState::Broken;

    const auto payload = QJsonDocument::fromJson(base64UrlDecode(token.left(dot))).object();
    const QString tokenNick = payload.value(QStringLiteral("nick")).toString();
    const double expires = payload.value(QStringLiteral("exp")).toDouble();
    if (tokenNick.isEmpty() || expires <= 0)
        return TokenState::Broken;

    if (expires * 1000.0 < static_cast<double>(QDateTime::currentMSecsSinceEpoch()))
        return TokenState::Expired;

    const QString current = nick();
    if (!current.isEmpty() && tokenNick.compare(current, Qt::CaseInsensitive) != 0)
        return TokenState::OtherNick;

    return TokenState::Ok;
}

QString TelegramAuth::tokenNick() const
{
    const QString path = tokenFilePath();
    if (path.isEmpty())
        return {};

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("nick")).toString();
}

bool TelegramAuth::useOfflineAccount(const QString& nick)
{
    auto* accounts = APPLICATION->accounts();
    if (!accounts || nick.isEmpty())
        return false;

    auto account = accounts->getAccountByProfileName(nick);
    if (!account) {
        account = MinecraftAccount::createOffline(nick);
        if (!account)
            return false;
        accounts->addAccount(account);
    }

    accounts->setDefaultAccount(account);
    accounts->saveList();
    return true;
}

}  // namespace berry
