// SPDX-License-Identifier: GPL-3.0-only
/*
 *  BerryTrip Launcher - single server build of Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "berry/BerryConfig.h"

#include <QDir>

#include "Application.h"
#include "InstanceList.h"
#include "minecraft/MinecraftInstance.h"

namespace berry {

namespace {
constexpr auto InstanceId = "BerryTrip";
constexpr auto ServerHost = "135.181.126.167";
constexpr quint16 ServerPort = 25737;
constexpr auto PackBaseUrl = "https://evilvalue.github.io/berry/";
constexpr auto AuthApiUrl = "http://135.181.126.167:25712";
constexpr auto LauncherVersion = "0.2.0";
}  // namespace

QString Config::version()
{
    return QString::fromLatin1(LauncherVersion);
}

QString Config::instanceId()
{
    return QString::fromLatin1(InstanceId);
}

QString Config::instanceName()
{
    return instanceId();
}

QString Config::serverHost()
{
    return QString::fromLatin1(ServerHost);
}

quint16 Config::serverPort()
{
    return ServerPort;
}

QString Config::serverAddress()
{
    return QStringLiteral("%1:%2").arg(serverHost()).arg(serverPort());
}

QString Config::authApi()
{
    return QString::fromLatin1(AuthApiUrl);
}

QUrl Config::packIndexUrl()
{
    return QUrl(QString::fromLatin1(PackBaseUrl) + QStringLiteral("index.toml"));
}

QUrl Config::packFileUrl(const QString& relativePath)
{
    // Packwiz paths are relative to the pack root and may contain spaces.
    const auto encoded = QString::fromLatin1(QUrl::toPercentEncoding(relativePath, QByteArrayLiteral("/")));
    return QUrl(QString::fromLatin1(PackBaseUrl) + encoded);
}

QString Config::instancesRoot()
{
    auto* list = APPLICATION ? APPLICATION->instances() : nullptr;
    return list ? list->primaryDir() : QString();
}

QString Config::instanceRoot()
{
    const QString root = instancesRoot();
    return root.isEmpty() ? QString() : QDir(root).filePath(instanceId());
}

QString Config::gameRoot()
{
    auto* list = APPLICATION ? APPLICATION->instances() : nullptr;
    if (list) {
        if (auto* instance = list->getInstanceById(instanceId()))
            return instance->gameRoot();
    }
    const QString root = instanceRoot();
    return root.isEmpty() ? QString() : QDir(root).filePath(QStringLiteral("minecraft"));
}

}  // namespace berry
