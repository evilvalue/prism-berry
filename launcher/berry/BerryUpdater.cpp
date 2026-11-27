// SPDX-License-Identifier: GPL-3.0-only
/*
 *  BerryTrip Launcher - single server build of Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "berry/BerryUpdater.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QStandardPaths>
#include <QTextStream>
#include <QVersionNumber>

#include "berry/BerryConfig.h"

namespace berry {

namespace {
constexpr auto ReleasesApi = "https://api.github.com/repos/evilvalue/prism-berry/releases/latest";
constexpr qint64 MinInstallerSize = 5 * 1024 * 1024;
}  // namespace

Updater::Updater(QObject* parent) : QObject(parent), m_network(new QNetworkAccessManager(this)) {}

QString Updater::normalizedVersion(QString version)
{
    version = version.trimmed();
    if (version.startsWith(QLatin1Char('v')) || version.startsWith(QLatin1Char('V')))
        version.remove(0, 1);
    const int suffix = version.indexOf(QLatin1Char('-'));
    if (suffix > 0)
        version = version.left(suffix);
    return version;
}

bool Updater::isNewer(const QString& candidate, const QString& current)
{
    const QVersionNumber remote = QVersionNumber::fromString(normalizedVersion(candidate));
    const QVersionNumber local = QVersionNumber::fromString(normalizedVersion(current));
    if (remote.isNull() || local.isNull())
        return normalizedVersion(candidate) != normalizedVersion(current);
    return QVersionNumber::compare(remote, local) > 0;
}

void Updater::check()
{
    if (m_checkReply || m_downloading)
        return;

    m_installerReady = false;
    m_installerPath.clear();
    m_installerUrl.clear();
    m_digest.clear();
    m_expectedSize = -1;

    QNetworkRequest request(QUrl(QString::fromLatin1(ReleasesApi)));
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("BerryTrip/") + Config::version());
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(15000);

    emit checking();
    m_checkReply = m_network->get(request);
    connect(m_checkReply, &QNetworkReply::finished, this, [this] {
        auto* reply = m_checkReply;
        m_checkReply = nullptr;
        if (reply)
            handleCheckReply(reply);
    });
}

void Updater::handleCheckReply(QNetworkReply* reply)
{
    const QByteArray body = reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto error = reply->error();
    const QString networkError = reply->errorString();
    reply->deleteLater();

    if (error != QNetworkReply::NoError || status != 200) {
        emit checkFailed(status > 0 ? tr("GitHub ответил HTTP %1.").arg(status) : networkError);
        return;
    }

    const QJsonObject release = QJsonDocument::fromJson(body).object();
    const QString tag = release.value(QStringLiteral("tag_name")).toString();
    if (tag.isEmpty()) {
        emit checkFailed(tr("В ответе GitHub нет номера версии."));
        return;
    }

    const QString latest = normalizedVersion(tag);
    if (!isNewer(latest, Config::version())) {
        emit upToDate(Config::version());
        return;
    }

    QUrl installerUrl;
    QString digest;
    qint64 size = -1;
    const QJsonArray assets = release.value(QStringLiteral("assets")).toArray();
    const QString preferred = QStringLiteral("BerryTrip-Setup-%1.exe").arg(latest);
    for (const auto& value : assets) {
        const QJsonObject asset = value.toObject();
        const QString name = asset.value(QStringLiteral("name")).toString();
        if (!name.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive))
            continue;
        if (!name.startsWith(QStringLiteral("BerryTrip-Setup-"), Qt::CaseInsensitive))
            continue;
        if (!installerUrl.isEmpty() && name != preferred)
            continue;

        installerUrl = QUrl(asset.value(QStringLiteral("browser_download_url")).toString());
        digest = asset.value(QStringLiteral("digest")).toString();
        size = static_cast<qint64>(asset.value(QStringLiteral("size")).toDouble(-1));
        if (name == preferred)
            break;
    }

    m_version = latest;
    m_notes = release.value(QStringLiteral("body")).toString();
    m_releasePage = QUrl(release.value(QStringLiteral("html_url")).toString());
    m_installerUrl = installerUrl;
    m_digest = digest;
    m_expectedSize = size;

    emit available(m_version, m_notes, m_releasePage, m_installerUrl, m_digest, m_expectedSize);
}

void Updater::download()
{
    if (m_downloading || m_installerUrl.isEmpty())
        return;

    const QString fileName = QFileInfo(m_installerUrl.path()).fileName();
    if (fileName.isEmpty() || !fileName.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) {
        emit failed(tr("У релиза нет установщика для автообновления."));
        return;
    }

    const QString folder = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                               .filePath(QStringLiteral("BerryTrip"));
    if (!QDir().mkpath(folder)) {
        emit failed(tr("Не удалось создать папку для загрузки."));
        return;
    }

    m_installerPath = QDir(folder).filePath(fileName);
    m_installerReady = false;
    QFile::remove(m_installerPath);

    m_file = new QFile(m_installerPath, this);
    if (!m_file->open(QIODevice::WriteOnly)) {
        const QString error = m_file->errorString();
        m_file->deleteLater();
        m_file = nullptr;
        emit failed(tr("Не удалось открыть файл обновления: %1").arg(error));
        return;
    }

    m_downloading = true;
    emit downloading(m_version);

    QNetworkRequest request(m_installerUrl);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("BerryTrip/") + Config::version());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(60000);

    m_downloadReply = m_network->get(request);

    connect(m_downloadReply, &QNetworkReply::readyRead, this, [this] {
        if (m_file)
            m_file->write(m_downloadReply->readAll());
    });
    connect(m_downloadReply, &QNetworkReply::downloadProgress, this, &Updater::downloadProgress);
    connect(m_downloadReply, &QNetworkReply::finished, this, [this] {
        auto* reply = m_downloadReply;
        m_downloadReply = nullptr;
        if (reply)
            handleDownloadReply(reply);
    });
}

void Updater::failDownload(const QString& error)
{
    m_downloading = false;
    if (m_file) {
        m_file->close();
        m_file->deleteLater();
        m_file = nullptr;
    }
    if (!m_installerPath.isEmpty())
        QFile::remove(m_installerPath);
    m_installerReady = false;
    emit failed(error);
}

void Updater::handleDownloadReply(QNetworkReply* reply)
{
    if (m_file) {
        m_file->write(reply->readAll());
        m_file->flush();
        m_file->close();
        m_file->deleteLater();
        m_file = nullptr;
    }

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto error = reply->error();
    const QString networkError = reply->errorString();
    reply->deleteLater();

    if (error != QNetworkReply::NoError || (status != 0 && status != 200)) {
        failDownload(status > 0 ? tr("Скачивание не удалось: HTTP %1.").arg(status) : networkError);
        return;
    }

    QFileInfo info(m_installerPath);
    if (!info.exists() || info.size() < MinInstallerSize) {
        failDownload(tr("Скачанный установщик повреждён или слишком мал."));
        return;
    }
    if (m_expectedSize > 0 && info.size() != m_expectedSize) {
        failDownload(tr("Размер установщика не совпал с данными GitHub."));
        return;
    }

    if (m_digest.startsWith(QStringLiteral("sha256:"), Qt::CaseInsensitive)) {
        QFile file(m_installerPath);
        if (!file.open(QIODevice::ReadOnly)) {
            failDownload(tr("Не удалось проверить SHA-256 установщика."));
            return;
        }
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!hash.addData(&file)) {
            failDownload(tr("Не удалось прочитать установщик для проверки SHA-256."));
            return;
        }
        const QByteArray actual = hash.result().toHex();
        const QByteArray expected = m_digest.mid(7).toLatin1().toLower();
        if (actual != expected) {
            failDownload(tr("SHA-256 установщика не совпал с данными GitHub."));
            return;
        }
    }

    m_downloading = false;
    m_installerReady = true;
    emit readyToInstall(m_installerPath);
}

bool Updater::install(QString* error)
{
    if (!m_installerReady || !QFileInfo::exists(m_installerPath)) {
        if (error)
            *error = tr("Установщик ещё не скачан.");
        return false;
    }

    const QString scriptPath = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                                   .filePath(QStringLiteral("BerryTrip/berrytrip-update.cmd"));
    QFile script(scriptPath);
    if (!script.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (error)
            *error = tr("Не удалось создать помощник обновления: %1").arg(script.errorString());
        return false;
    }

    QTextStream out(&script);
    out << "@echo off\r\n";
    out << "timeout /t 2 /nobreak >nul\r\n";
    out << "start \"\" /wait \"" << QDir::toNativeSeparators(m_installerPath) << "\" /S\r\n";
    out << "start \"\" \"" << QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) << "\"\r\n";
    script.close();

    if (!QProcess::startDetached(QStringLiteral("cmd.exe"),
                                 { QStringLiteral("/c"), QDir::toNativeSeparators(scriptPath) })) {
        if (error)
            *error = tr("Не удалось запустить установщик обновления.");
        return false;
    }
    return true;
}

}  // namespace berry
