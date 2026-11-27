// SPDX-License-Identifier: GPL-3.0-only
/*
 *  BerryTrip Launcher - single server build of Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "berry/BerryTweaks.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QSettings>

#include <utility>

#include "Application.h"
#include "berry/BerryConfig.h"

namespace berry {

namespace {

constexpr auto OffDirName = "berry-off";

QString settingsPath()
{
    return QDir(APPLICATION->dataRoot()).filePath(QStringLiteral("berry.ini"));
}

QString gameDir()
{
    return Config::gameRoot();
}

QString modsDir()
{
    return QDir(gameDir()).filePath(QStringLiteral("mods"));
}

QString offDir()
{
    return QDir(gameDir()).filePath(QString::fromLatin1(OffDirName));
}

QStringList jarsIn(const QString& dir)
{
    QStringList names;
    const QDir folder(dir);
    if (!folder.exists())
        return names;
    for (const auto& entry : folder.entryList({ QStringLiteral("*.jar") }, QDir::Files))
        names.append(entry);
    return names;
}

bool matchesPrefix(const QString& fileName, const QStringList& prefixes)
{
    const QString lower = fileName.toLower();
    for (const QString& prefix : prefixes) {
        if (lower.startsWith(prefix.toLower()))
            return true;
    }
    return false;
}

bool moveFile(const QString& from, const QString& to)
{
    if (QFileInfo::exists(to) && !QFile::remove(to))
        return false;
    return QFile::rename(from, to);
}

int setEnabled(const QStringList& prefixes, bool enabled)
{
    const QString mods = modsDir();
    const QString off = offDir();
    if (!enabled)
        QDir().mkpath(off);

    int moved = 0;
    const QString source = enabled ? off : mods;
    const QString target = enabled ? mods : off;
    for (const QString& name : jarsIn(source)) {
        if (!matchesPrefix(name, prefixes))
            continue;
        if (moveFile(QDir(source).filePath(name), QDir(target).filePath(name)))
            ++moved;
    }
    return moved;
}

void deleteIfEmpty(const QString& path)
{
    QDir dir(path);
    if (dir.exists() && dir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot).isEmpty())
        dir.removeRecursively();
}

void applyPacks(bool enabled)
{
    const QString path = QDir(gameDir()).filePath(QStringLiteral("options.txt"));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    file.close();

    int packsIndex = -1;
    for (int i = 0; i < lines.size(); ++i) {
        if (lines.at(i).startsWith(QStringLiteral("resourcePacks:"), Qt::CaseInsensitive))
            packsIndex = i;
    }
    if (packsIndex < 0)
        return;

    if (enabled) {
        QSettings settings(settingsPath(), QSettings::IniFormat);
        const QString saved = settings.value(QStringLiteral("tweaks/packs_saved")).toString();
        if (!saved.isEmpty())
            lines[packsIndex] = QStringLiteral("resourcePacks:") + saved;
    } else {
        const QString current = lines.at(packsIndex).mid(QStringLiteral("resourcePacks:").size());
        if (current != QLatin1String("[]")) {
            QSettings settings(settingsPath(), QSettings::IniFormat);
            settings.setValue(QStringLiteral("tweaks/packs_saved"), current);
        }
        lines[packsIndex] = QStringLiteral("resourcePacks:[]");
    }

    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    out.write(lines.join(QLatin1Char('\n')).toUtf8());
    out.commit();
}

void applyShaders(bool enabled)
{
    const QString path = QDir(gameDir()).filePath(QStringLiteral("config/iris.properties"));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    file.close();

    bool found = false;
    for (QString& line : lines) {
        if (line.trimmed().toLower().startsWith(QStringLiteral("enableshaders="))) {
            line = QStringLiteral("enableShaders=") + (enabled ? QStringLiteral("true") : QStringLiteral("false"));
            found = true;
        }
    }
    if (!found)
        lines.append(QStringLiteral("enableShaders=") + (enabled ? QStringLiteral("true") : QStringLiteral("false")));

    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    out.write(lines.join(QLatin1Char('\n')).toUtf8());
    out.commit();
}

/// DH names LOD folders after the server; Cyrillic names make paths longer than
/// Windows can open, so force IP_ONLY and move old percent-encoded folders over.
void applyDistantHorizons()
{
    const QString configDir = QDir(gameDir()).filePath(QStringLiteral("config"));
    const QString configPath = QDir(configDir).filePath(QStringLiteral("DistantHorizons.toml"));

    if (!QFileInfo::exists(configPath)) {
        QDir().mkpath(configDir);
        QFile bundled(QStringLiteral(":/berry/DistantHorizons.toml"));
        if (bundled.open(QIODevice::ReadOnly)) {
            QSaveFile out(configPath);
            if (out.open(QIODevice::WriteOnly)) {
                out.write(bundled.readAll());
                out.commit();
            }
        }
    }

    QFile config(configPath);
    if (config.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QStringList lines = QString::fromUtf8(config.readAll()).split(QLatin1Char('\n'));
        config.close();

        bool changed = false;
        for (QString& line : lines) {
            if (!line.trimmed().toLower().startsWith(QStringLiteral("serverfoldernamemode")))
                continue;
            const int equals = line.indexOf(QLatin1Char('='));
            if (equals < 0)
                continue;
            if (line.mid(equals + 1).trimmed().compare(QStringLiteral("\"IP_ONLY\""), Qt::CaseInsensitive) == 0)
                continue;
            line = line.left(equals + 1) + QStringLiteral(" \"IP_ONLY\"");
            changed = true;
        }
        if (changed) {
            QSaveFile out(configPath);
            if (out.open(QIODevice::WriteOnly | QIODevice::Text)) {
                out.write(lines.join(QLatin1Char('\n')).toUtf8());
                out.commit();
            }
        }
    }

    const QString root = QDir(gameDir()).filePath(QStringLiteral("Distant_Horizons_server_data"));
    QDir rootDir(root);
    if (!rootDir.exists())
        return;

    const QString target = QDir(root).filePath(Config::serverHost());
    for (const QString& name : rootDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!name.contains(QLatin1Char('%')))
            continue;
        const QString source = QDir(root).filePath(name);
        QDir().mkpath(target);
        QDirIterator it(source, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            const QString relative = QDir(source).relativeFilePath(path);
            const QString destination = QDir(target).filePath(relative);
            if (QFileInfo(path).isDir()) {
                QDir().mkpath(destination);
            } else if (!QFileInfo::exists(destination)) {
                QDir().mkpath(QFileInfo(destination).absolutePath());
                QFile::rename(path, destination);
            }
        }
        rootDir.removeRecursively();
    }
}

QByteArray hashFile(const QString& path, QCryptographicHash::Algorithm algorithm)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(algorithm);
    if (!hash.addData(&file))
        return {};
    return hash.result();
}

void download(const QUrl& url, const QString& target, std::function<void(bool, const QString&)> done)
{
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(60000);

    auto* file = new QSaveFile(target);
    QDir().mkpath(QFileInfo(target).absolutePath());
    if (!file->open(QIODevice::WriteOnly)) {
        delete file;
        done(false, QStringLiteral("не удалось открыть файл"));
        return;
    }

    auto* reply = APPLICATION->network()->get(request);

    QObject::connect(reply, &QNetworkReply::readyRead, file, [reply, file] { file->write(reply->readAll()); });
    QObject::connect(reply, &QNetworkReply::finished, file, [reply, file, target, done = std::move(done)] {
        file->write(reply->readAll());
        const bool ok = reply->error() == QNetworkReply::NoError && file->commit();
        const QString error = ok ? QString() : reply->errorString();
        file->deleteLater();
        reply->deleteLater();
        done(ok, error);
    });
}

}  // namespace

const QVector<Tweaks::Mod>& Tweaks::mods()
{
    static const QVector<Mod> list{
        { QStringLiteral("shouldersurfing"), QStringLiteral("ShoulderSurfing"), QStringLiteral("вид от плеча"), true,
          { QStringLiteral("shouldersurfing") }, {}, {}, {}, {}, 0 },
        { QStringLiteral("lights"), QStringLiteral("LambDynamicLights"), QStringLiteral("динамический свет"), true,
          { QStringLiteral("lambdynamiclights") }, {}, {}, {}, {}, 0 },
        { QStringLiteral("tooltips"), QStringLiteral("Obscure Tooltips"), QStringLiteral("подсказки предметов"), true,
          { QStringLiteral("obscure_tooltips") }, {}, {}, {}, {}, 0 },
        { QStringLiteral("quickskin"), QStringLiteral("Quick Skin"), QStringLiteral("скины игроков"), true,
          { QStringLiteral("quick_skin") }, {}, {}, {}, {}, 0 },
        { QStringLiteral("distanthorizons"), QStringLiteral("Distant Horizons"),
          QStringLiteral("дальние чанки; по умолчанию выключен"), false, { QStringLiteral("distanthorizons") },
          QStringLiteral("https://cdn.modrinth.com/data/uCdwusMi/versions/9w34y8ai/DistantHorizons-3.3.3-1.21.1-fabric-neoforge.jar"),
          QStringLiteral("https://mediafilez.forgecdn.net/files/9004/764/DistantHorizons-3.3.3-1.21.1-fabric-neoforge.jar"),
          QStringLiteral("d14b32d3fd143a06514d6ad388eee11e9d657c84e1a937aaf8d033b1137fb43ef8b05cff1011a994cc42f3106823c51234f8bc18d72bb05bc317a20ea83c50ed"),
          QStringLiteral("DistantHorizons-3.3.3-1.21.1-fabric-neoforge.jar"), 28289212 },
    };
    return list;
}

bool Tweaks::modEnabled(const Mod& mod)
{
                QSettings settings(settingsPath(), QSettings::IniFormat);
    return settings.value(QStringLiteral("tweaks/mod.") + mod.key, mod.defaultOn).toBool();
}

void Tweaks::setModEnabled(const Mod& mod, bool enabled)
{
                QSettings settings(settingsPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("tweaks/mod.") + mod.key, enabled);
}

bool Tweaks::packsEnabled()
{
                QSettings settings(settingsPath(), QSettings::IniFormat);
    return settings.value(QStringLiteral("tweaks/packs"), true).toBool();
}

void Tweaks::setPacksEnabled(bool enabled)
{
                QSettings settings(settingsPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("tweaks/packs"), enabled);
}

bool Tweaks::shadersEnabled()
{
                QSettings settings(settingsPath(), QSettings::IniFormat);
    return settings.value(QStringLiteral("tweaks/shaders"), false).toBool();
}

void Tweaks::setShadersEnabled(bool enabled)
{
                QSettings settings(settingsPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("tweaks/shaders"), enabled);
}

void Tweaks::applyAll()
{
    if (gameDir().isEmpty())
        return;

    for (const Mod& mod : mods())
        setEnabled(mod.prefixes, modEnabled(mod));

    setEnabled({ QStringLiteral("entity_model_features"), QStringLiteral("entity_texture_features") }, packsEnabled());
    for (const QString& locked : { QStringLiteral("journeymap"), QStringLiteral("immersive_paintings"),
                                   QStringLiteral("emotecraft"), QStringLiteral("pop_up_emotes"),
                                   QStringLiteral("notenoughanimations") })
        setEnabled({ locked }, true);

    applyPacks(packsEnabled());
    applyShaders(shadersEnabled());
    applyDistantHorizons();
    deleteIfEmpty(offDir());
}

void Tweaks::installOptionalMod(const Mod& mod, std::function<void(bool, const QString&)> done)
{
    if (!mod.downloadable()) {
        done(false, QStringLiteral("для мода не задан адрес загрузки"));
        return;
    }

    const QString target = QDir(modsDir()).filePath(mod.filename);
    QDir().mkpath(modsDir());

    if (QFileInfo::exists(target) && QFileInfo(target).size() == mod.size && !mod.sha512.isEmpty() &&
        hashFile(target, QCryptographicHash::Sha512).toHex() == mod.sha512.toLatin1()) {
        done(true, QString());
        return;
    }

    const auto verifyAndFinish = [mod, target, done](bool ok, const QString& error) {
        if (!ok) {
            done(false, error);
            return;
        }
        const QString actual = QString::fromLatin1(hashFile(target, QCryptographicHash::Sha512).toHex());
        if (!mod.sha512.isEmpty() && actual.compare(mod.sha512, Qt::CaseInsensitive) != 0) {
            QFile::remove(target);
            done(false, QStringLiteral("файл не совпал по SHA-512"));
            return;
        }
        done(true, QString());
    };

    if (!mod.urlFallback.isEmpty()) {
        download(QUrl(mod.url), target, [mod, target, verifyAndFinish](bool ok, const QString& error) {
            if (ok) {
                verifyAndFinish(true, QString());
                return;
            }
            download(QUrl(mod.urlFallback), target, verifyAndFinish);
        });
    } else {
        download(QUrl(mod.url), target, verifyAndFinish);
    }
}

int Tweaks::modCount()
{
    return jarsIn(modsDir()).size();
}

int Tweaks::offCount()
{
    return jarsIn(offDir()).size();
}

QString Tweaks::resetConfigs()
{
    const QString root = gameDir();
    if (root.isEmpty())
        return {};

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
    const QString backup = QDir(Config::instanceRoot()).filePath(QStringLiteral("berry-config-backup/") + stamp);
    QDir().mkpath(backup);

    const QString tokenPath = QDir(root).filePath(QStringLiteral("config/berryauth-token.json"));
    QByteArray token;
    if (QFile file(tokenPath); file.open(QIODevice::ReadOnly))
        token = file.readAll();

    const QStringList files{ QStringLiteral("options.txt"), QStringLiteral("servers.dat"),
                             QStringLiteral("servers.dat_old"), QStringLiteral("keybindings.txt") };
    const QStringList dirs{ QStringLiteral("config"), QStringLiteral("defaultconfigs") };

    for (const QString& file : files) {
        const QString path = QDir(root).filePath(file);
        if (QFileInfo::exists(path)) {
            QFile::copy(path, QDir(backup).filePath(file));
            QFile::remove(path);
        }
    }
    for (const QString& dir : dirs) {
        const QString path = QDir(root).filePath(dir);
        if (!QFileInfo::exists(path))
            continue;
        QDirIterator it(path, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString file = it.next();
            const QString target = QDir(backup).filePath(QDir(root).relativeFilePath(file));
            QDir().mkpath(QFileInfo(target).absolutePath());
            QFile::copy(file, target);
        }
        QDir(path).removeRecursively();
    }

    if (!token.isEmpty()) {
        QDir().mkpath(QFileInfo(tokenPath).absolutePath());
        QSaveFile out(tokenPath);
        if (out.open(QIODevice::WriteOnly)) {
            out.write(token);
            out.commit();
        }
    }

    return backup;
}

QString Tweaks::summary()
{
    return QStringLiteral("%1 модов · %2 в berry-off · ресурспаки %3 · шейдеры %4")
        .arg(modCount())
        .arg(offCount())
        .arg(packsEnabled() ? QStringLiteral("вкл") : QStringLiteral("выкл"))
        .arg(shadersEnabled() ? QStringLiteral("вкл") : QStringLiteral("выкл"));
}

}  // namespace berry
