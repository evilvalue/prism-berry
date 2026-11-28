// SPDX-License-Identifier: GPL-3.0-only
/*
 *  BerryTrip Launcher - single server build of Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "berry/BerryAuth.h"

#include <QApplication>
#include <QClipboard>
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
#include <QProcess>
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
    // Older builds kept the pending login next to the nick; the one-time
    // secret does not belong in a plain-text file, so drop the leftovers.
    QSettings store(settingsPath(), QSettings::IniFormat);
    if (store.contains(QStringLiteral("auth/request_id")))
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
        if (!m_pendingNick.isEmpty() && clean.compare(m_pendingNick, Qt::CaseInsensitive) != 0) {
            emit message(tr("Уже открыт вход для %1. Заверши его или выйди и начни снова.").arg(m_pendingNick), true);
            return;
        }
        if (!openPendingLink()) {
            emit message(tr("Не удалось открыть Telegram. Скопируй ссылку кнопкой ниже."), true);
            return;
        }
        emit message(tr("Telegram открыт — нажмите Start в боте."), false);
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
        qint64 expires = static_cast<qint64>(json.value(QStringLiteral("expires")).toDouble());
        // The bot API has used both Unix seconds and milliseconds for this
        // field; accept either instead of declaring a fresh link expired.
        if (expires > 0 && expires < 1000000000000LL)
            expires *= 1000;

        const QUrl botLink(link);
        if (id.isEmpty() || secret.isEmpty() || !botLink.isValid() ||
            (botLink.scheme() != QLatin1String("https") && botLink.scheme() != QLatin1String("http")) ||
            botLink.host().compare(QLatin1String("t.me"), Qt::CaseInsensitive) != 0 ||
            expires <= QDateTime::currentMSecsSinceEpoch()) {
            emit message(tr("Сервер выдал некорректную ссылку. Попробуйте ещё раз."), true);
            return;
        }

        m_pendingId = id;
        m_pendingSecret = secret;
        m_pendingNick = nick;
        m_pendingLink = link;
        m_pendingUntil = expires;

        if (openTelegramLink(link)) {
            emit message(tr("Telegram открыт — нажмите Start в боте."), false);
        } else {
            QApplication::clipboard()->setText(link);
            emit message(tr("Не удалось открыть Telegram. Ссылка скопирована — открой её вручную."), true);
        }
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
    return openTelegramLink(m_pendingLink);
}

bool TelegramAuth::openTelegramLink(const QString& link)
{
    const QUrl https(link);
    // Only Telegram links are opened here. Anything else (including a value
    // planted in the config) is ignored instead of being handed to the
    // desktop URL handler.
    if (!https.isValid() || https.scheme() != QLatin1String("https"))
        return false;

    const QString host = https.host().toLower();
    if (host != QLatin1String("t.me") && host != QLatin1String("telegram.me"))
        return false;

    // "killberriesbot?start=..." -> bot name without a trailing slash.
    const QString path = https.path().mid(1);
    const QString bot = path.section(QLatin1Char('/'), 0, 0);
    if (bot.isEmpty())
        return QDesktopServices::openUrl(https);

    const QUrlQuery query(https);
    QString startToken = query.queryItemValue(QStringLiteral("start"));
    if (startToken.isEmpty() && path.contains(QLatin1Char('/')))
        startToken = path.section(QLatin1Char('/'), 1, 1);

    QUrl deep(QStringLiteral("tg://resolve"));
    QUrlQuery deepQuery;
    deepQuery.addQueryItem(QStringLiteral("domain"), bot);
    if (!startToken.isEmpty())
        deepQuery.addQueryItem(QStringLiteral("start"), startToken);
    deep.setQuery(deepQuery);

#ifdef Q_OS_WIN
    // Telegram Desktop requires the double dash before its URI argument.
    // Starting the app directly keeps the full ?start= token; the URL handler
    // used by QDesktopServices sometimes passes only the domain.
    const QString roaming = qEnvironmentVariable("APPDATA");
    const QString local = qEnvironmentVariable("LOCALAPPDATA");
    const QStringList candidates{
        local + QStringLiteral("/Programs/Telegram Desktop/Telegram.exe"),
        roaming + QStringLiteral("/Telegram Desktop/Telegram.exe"),
        QStringLiteral("C:/Program Files/Telegram Desktop/Telegram.exe"),
        QStringLiteral("C:/Program Files (x86)/Telegram Desktop/Telegram.exe"),
    };
    for (const QString& executable : candidates) {
        if (executable.isEmpty() || !QFileInfo::exists(executable))
            continue;
        if (QProcess::startDetached(executable, { QStringLiteral("--"), deep.toString(QUrl::FullyEncoded) }))
            return true;
    }
#endif

    // No desktop client: the t.me URL is canonical and keeps ?start= intact.
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

    QString tokenNick;
    double expires = 0;
    const QStringList segments = token.split(QLatin1Char('.'));
    for (int i = 0; i < segments.size() && i < 2; ++i) {
        const auto claims = QJsonDocument::fromJson(base64UrlDecode(segments.at(i))).object();
        const QString nick = claims.value(QStringLiteral("nick")).toString();
        const double exp = claims.value(QStringLiteral("exp")).toDouble();
        if (!nick.isEmpty() && exp > 0) {
            tokenNick = nick;
            expires = exp;
            break;
        }
    }
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

    // Pick only an offline account. An MSA account with the same profile name
    // must never be selected, otherwise the game would try to refresh it.
    MinecraftAccountPtr account;
    for (int i = 0; i < accounts->count(); ++i) {
        const auto candidate = accounts->at(i);
        if (candidate->accountType() == AccountType::Offline &&
            candidate->profileName().compare(nick, Qt::CaseInsensitive) == 0) {
            account = candidate;
            break;
        }
    }
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
