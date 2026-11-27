// SPDX-License-Identifier: GPL-3.0-only
/*
 *  BerryTrip Launcher - single server build of Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "berry/BerryPackSync.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QtConcurrent>

#include <toml++/toml.h>

#include <optional>
#include <string>
#include <string_view>

#include "Application.h"
#include "StringUtils.h"
#include "berry/BerryConfig.h"

namespace berry {

namespace {
constexpr int kMaxParallelDownloads = 4;
}

PackSync::PackSync(QObject* parent) : QObject(parent) {}

void PackSync::start()
{
    if (m_running)
        return;

    m_root = Config::gameRoot();
    if (m_root.isEmpty()) {
        emit finished(false, QStringLiteral("Не найдена папка игры инстанса"));
        return;
    }

    m_entries.clear();
    m_queue.clear();
    m_queuedPaths.clear();
    m_doneEntries.clear();
    m_errors.clear();
    m_total = 0;
    m_completed = 0;
    m_inFlight = 0;
    m_pendingMetafiles = 0;
    m_running = true;

    fetchIndex();
}

void PackSync::abort()
{
    if (!m_running)
        return;

    m_running = false;
    m_queue.clear();
    const auto replies = m_replies;
    m_replies.clear();
    for (auto* reply : replies)
        reply->abort();

    emit finished(false, QStringLiteral("Обновление отменено"));
}

void PackSync::fail(const QString& message)
{
    m_errors.append(message);
    qWarning() << "BerryTrip sync:" << message;
}

void PackSync::fetchIndex()
{
    QNetworkRequest request(Config::packIndexUrl());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    auto* reply = APPLICATION->network()->get(request);
    m_replies.insert(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        m_replies.remove(reply);
        reply->deleteLater();
        if (!m_running)
            return;

        if (reply->error() != QNetworkReply::NoError) {
            m_running = false;
            emit finished(false, QStringLiteral("Не удалось скачать манифест: %1").arg(reply->errorString()));
            return;
        }

        parseIndex(reply->readAll());
    });
}

void PackSync::parseIndex(const QByteArray& data)
{
    toml::table index;
    try {
        index = toml::parse(std::string_view(data.constData(), static_cast<size_t>(data.size())));
    } catch (const toml::parse_error& error) {
        m_running = false;
        emit finished(false, QStringLiteral("Манифест повреждён: %1").arg(QString::fromStdString(std::string(error.description()))));
        return;
    }

    const auto defaultFormat = index["hash-format"].value<std::string>().value_or("sha256");
    const auto* files = index["files"].as_array();
    if (!files || files->empty()) {
        m_running = false;
        emit finished(false, QStringLiteral("В манифесте нет списка файлов"));
        return;
    }

    for (const auto& node : *files) {
        const auto* file = node.as_table();
        if (!file)
            continue;

        const auto path = (*file)["file"].value<std::string>();
        const auto hash = (*file)["hash"].value<std::string>();
        if (!path || !hash)
            continue;

        Entry entry;
        entry.hashFormat = QString::fromStdString((*file)["hash-format"].value<std::string>().value_or(defaultFormat));
        entry.hash = QByteArray::fromHex(QByteArray::fromStdString(*hash).toLower());
        entry.metafile = (*file)["metafile"].value_or(false);
        m_entries.insert(QString::fromStdString(*path), entry);
    }

    if (m_entries.isEmpty()) {
        m_running = false;
        emit finished(false, QStringLiteral("Манифест не содержит файлов"));
        return;
    }

    m_total = static_cast<int>(m_entries.size());
    scanLocalFiles();
}

void PackSync::scanLocalFiles()
{
    const QString root = m_root;
    const auto entries = m_entries;

    auto* watcher = new QFutureWatcher<Plan>(this);
    connect(watcher, &QFutureWatcher<Plan>::finished, this, [this, watcher] {
        const Plan plan = watcher->result();
        watcher->deleteLater();
        if (!m_running)
            return;

        for (const QString& error : plan.errors)
            fail(error);
        for (const QString& path : plan.satisfied)
            completeEntry(path);
        for (const Job& job : plan.jobs)
            enqueue(job);

        startNextDownloads();
        maybeFinish();
    });

    // Hashing the whole game folder and every .pw.toml used to run on the UI
    // thread, which froze the window for seconds on a large pack.
    watcher->setFuture(QtConcurrent::run([root, entries] { return buildPlan(root, entries); }));
}

PackSync::Plan PackSync::buildPlan(const QString& root, const QHash<QString, Entry>& entries)
{
    Plan plan;
    QSet<QString> queued;
    const auto add = [&](const Job& job) {
        if (queued.contains(job.diskPath))
            return false;
        queued.insert(job.diskPath);
        plan.jobs.append(job);
        return true;
    };

    for (auto it = entries.cbegin(); it != entries.cend(); ++it) {
        if (it->metafile)
            continue;

        const QString path = QDir(root).filePath(it.key());
        if (hashFile(path, it->hashFormat) == it->hash) {
            plan.satisfied.append(it.key());
            continue;
        }

        Job job;
        job.kind = Job::Kind::Blob;
        job.entryPath = it.key();
        job.diskPath = it.key();
        job.url = Config::packFileUrl(it.key());
        job.hashFormat = it->hashFormat;
        job.hash = it->hash;
        add(job);
    }

    for (auto it = entries.cbegin(); it != entries.cend(); ++it) {
        if (!it->metafile)
            continue;

        const QString localPath = QDir(root).filePath(it.key());
        const bool metafileOk = QFileInfo::exists(localPath) && hashFile(localPath, it->hashFormat) == it->hash;

        if (metafileOk) {
            Job payload;
            QString error;
            if (readMetafile(localPath, it.key(), &payload, &error)) {
                const QString payloadPath = QDir(root).filePath(payload.diskPath);
                if (hashFile(payloadPath, payload.hashFormat) == payload.hash) {
                    plan.satisfied.append(it.key());
                    continue;
                }
                payload.entryPath = it.key();
                if (!add(payload)) {
                    // The same payload is already queued by its own index entry.
                    plan.satisfied.append(it.key());
                }
                continue;
            }
            plan.errors.append(QStringLiteral("%1: %2").arg(it.key(), error));
        }

        Job job;
        job.kind = Job::Kind::Metafile;
        job.entryPath = it.key();
        job.diskPath = it.key();
        job.url = Config::packFileUrl(it.key());
        job.hashFormat = it->hashFormat;
        job.hash = it->hash;
        add(job);
    }

    return plan;
}

void PackSync::enqueue(const Job& job)
{
    if (m_queuedPaths.contains(job.diskPath))
        return;
    m_queuedPaths.insert(job.diskPath);
    m_queue.enqueue(job);
}

void PackSync::startNextDownloads()
{
    while (m_running && m_inFlight < kMaxParallelDownloads && !m_queue.isEmpty())
        startDownload(m_queue.dequeue());
}

void PackSync::startDownload(const Job& job)
{
    const QString target = QDir(m_root).filePath(job.diskPath);
    QDir().mkpath(QFileInfo(target).absolutePath());

    auto* file = new QSaveFile(target);
    if (!file->open(QIODevice::WriteOnly)) {
        delete file;
        fail(QStringLiteral("%1: %2").arg(job.diskPath, tr("не удалось открыть файл для записи")));
        completeEntry(job.entryPath);
        startNextDownloads();
        maybeFinish();
        return;
    }

    auto* hasher = new QCryptographicHash(algorithmFor(job.hashFormat));

    QNetworkRequest request(job.url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    auto* reply = APPLICATION->network()->get(request);
    m_replies.insert(reply);
    ++m_inFlight;

    connect(reply, &QNetworkReply::readyRead, this, [reply, file, hasher] {
        const QByteArray chunk = reply->readAll();
        file->write(chunk);
        hasher->addData(chunk);
    });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, job, file, hasher] { onReplyFinished(reply, job, file, hasher); });
}

void PackSync::onReplyFinished(QNetworkReply* reply, const Job& job, QSaveFile* file, QCryptographicHash* hasher)
{
    --m_inFlight;
    m_replies.remove(reply);
    reply->deleteLater();

    const QByteArray tail = reply->readAll();
    if (!tail.isEmpty()) {
        file->write(tail);
        hasher->addData(tail);
    }
    const QByteArray actual = hasher->result();
    delete hasher;

    if (!m_running) {
        file->cancelWriting();
        delete file;
        return;
    }

    QString error;
    if (reply->error() != QNetworkReply::NoError) {
        error = reply->errorString();
    } else if (actual != job.hash) {
        error = tr("не совпала контрольная сумма");
    } else if (!file->commit()) {
        error = file->errorString();
    }
    delete file;

    if (!error.isEmpty()) {
        fail(QStringLiteral("%1: %2").arg(job.diskPath, error));
        completeEntry(job.entryPath);
        startNextDownloads();
        maybeFinish();
        return;
    }

    if (job.kind == Job::Kind::Metafile)
        onMetafileInstalled(job, QDir(m_root).filePath(job.diskPath));
    else
        completeEntry(job.entryPath);

    startNextDownloads();
    maybeFinish();
}

void PackSync::onMetafileInstalled(const Job& job, const QString& localPath)
{
    ++m_pendingMetafiles;
    const QString root = m_root;

    auto* watcher = new QFutureWatcher<MetafileResult>(this);
    connect(watcher, &QFutureWatcher<MetafileResult>::finished, this, [this, watcher, job] {
        const MetafileResult result = watcher->result();
        watcher->deleteLater();
        if (m_pendingMetafiles > 0)
            --m_pendingMetafiles;
        if (!m_running)
            return;

        if (!result.ok) {
            fail(QStringLiteral("%1: %2").arg(job.diskPath, result.error));
            completeEntry(job.entryPath);
        } else if (result.payloadReady) {
            completeEntry(job.entryPath);
        } else if (m_queuedPaths.contains(result.payload.diskPath)) {
            // Another metafile already queued the same payload; still account for this entry.
            completeEntry(job.entryPath);
        } else {
            enqueue(result.payload);
        }

        startNextDownloads();
        maybeFinish();
    });

    // Parsing a downloaded metafile and hashing its jar are also file work;
    // keep them off the UI thread.
    watcher->setFuture(QtConcurrent::run([root, localPath, job] {
        MetafileResult result;
        Job payload;
        QString error;
        if (!readMetafile(localPath, job.diskPath, &payload, &error)) {
            result.error = error;
            return result;
        }

        payload.entryPath = job.entryPath;
        result.ok = true;
        result.payload = payload;
        const QString payloadPath = QDir(root).filePath(payload.diskPath);
        result.payloadReady = QFileInfo::exists(payloadPath) && hashFile(payloadPath, payload.hashFormat) == payload.hash;
        return result;
    }));
}

void PackSync::completeEntry(const QString& entryPath)
{
    if (!m_running || entryPath.isEmpty() || m_doneEntries.contains(entryPath))
        return;

    m_doneEntries.insert(entryPath);
    ++m_completed;
    emit progress(m_completed, m_total, entryPath);
}

void PackSync::maybeFinish()
{
    if (!m_running || !m_queue.isEmpty() || m_inFlight > 0 || m_pendingMetafiles > 0)
        return;

    m_running = false;
    if (m_errors.isEmpty())
        emit finished(true, QString());
    else
        emit finished(false, m_errors.join(QLatin1Char('\n')));
}

bool PackSync::readMetafile(const QString& localPath, const QString& metafilePath, Job* payload, QString* error)
{
    toml::table table;
    try {
        table = toml::parse_file(StringUtils::toStdString(localPath));
    } catch (const toml::parse_error& parseError) {
        *error = QString::fromStdString(std::string(parseError.description()));
        return false;
    } catch (const std::exception& exception) {
        *error = QString::fromUtf8(exception.what());
        return false;
    }

    const auto filename = table["filename"].value<std::string>();
    const auto* download = table["download"].as_table();
    if (!filename || filename->empty() || !download) {
        *error = QStringLiteral("метафайл не содержит filename или [download]");
        return false;
    }

    const auto url = (*download)["url"].value<std::string>();
    const auto hash = (*download)["hash"].value<std::string>();
    if (!url || !hash) {
        *error = QStringLiteral("в метафайле нет url или hash");
        return false;
    }

    const QString dir = QFileInfo(metafilePath).path();
    const QString fileName = QString::fromStdString(*filename);

    payload->kind = Job::Kind::Blob;
    payload->diskPath = (dir.isEmpty() || dir == QLatin1String(".")) ? fileName : dir + QLatin1Char('/') + fileName;
    payload->url = QUrl(QString::fromStdString(*url));
    payload->hashFormat = QString::fromStdString((*download)["hash-format"].value<std::string>().value_or("sha512"));
    payload->hash = QByteArray::fromHex(QByteArray::fromStdString(*hash).toLower());
    return true;
}

QCryptographicHash::Algorithm PackSync::algorithmFor(const QString& format)
{
    const QString normalized = format.trimmed().toLower();
    if (normalized == QLatin1String("sha512"))
        return QCryptographicHash::Sha512;
    if (normalized == QLatin1String("sha1"))
        return QCryptographicHash::Sha1;
    if (normalized == QLatin1String("md5"))
        return QCryptographicHash::Md5;
    return QCryptographicHash::Sha256;
}

QByteArray PackSync::hashFile(const QString& path, const QString& format)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};

    QCryptographicHash hash(algorithmFor(format));
    if (!hash.addData(&file))
        return {};
    return hash.result();
}

}  // namespace berry
