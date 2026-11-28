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
// The mod serving this API runs inside the game server, which has no shell to
// terminate TLS itself, so the launcher talks HTTPS to a small relay instead.
// The plain-HTTP origin stays as a last-resort fallback.
// The mod now serves HTTPS itself: the server cannot host a proxy, so it
// terminates TLS on its own port. The certificate is self-signed, so the
// launcher trusts exactly this one and nothing else.
constexpr auto AuthApiTlsUrl = "https://135.181.126.167:25712";
constexpr auto AuthApiPinnedPem = R"CERT(-----BEGIN CERTIFICATE-----
MIIDSzCCAjOgAwIBAgIUUlW2AonVnX1RTHt9b4FJY7bE/T4wDQYJKoZIhvcNAQEL
BQAwGTEXMBUGA1UEAwwOYmVycnl0cmlwLWF1dGgwHhcNMjYxMTI4MDcwNjI0WhcN
MzYxMTI1MDcwNjI0WjAZMRcwFQYDVQQDDA5iZXJyeXRyaXAtYXV0aDCCASIwDQYJ
KoZIhvcNAQEBBQADggEPADCCAQoCggEBALLivo8lTt5KgguXlEcHSDfEDqOcSfw8
lACw3EiI5pUdOZCr7jVMvAsK+t0boQ6s58NI7ue1YJmvyHdne9LcfyrbIi506OMT
oSLIrW+G2sBmTUjSTnz7MQ+q0Gh0fsEQtWULKsdYEpq7ryQCCTI0sez/xcsH7F+R
RfWU844pRdRpX3ONLKrtlu4aIddCB+gOgkxeEi2CQg1ULhtIifMyTtvj2NnLWxvz
AW0r9Pgt4fofBygOx6ItqkT51cQzMZP0FQPgFebMT34YTVS0LiheKIylzkU+p9Ln
q/K/LXoHw8ZqqPwhw3P059f3YZB2FEMfYqbFrGhw/cyaQEe0eKj5CWsCAwEAAaOB
ijCBhzAdBgNVHQ4EFgQUoVyJOrj5FZpA1hfgstPjNq/8bJQwHwYDVR0jBBgwFoAU
oVyJOrj5FZpA1hfgstPjNq/8bJQwDwYDVR0RBAgwBocEh7V+pzAPBgNVHRMBAf8E
BTADAQH/MA4GA1UdDwEB/wQEAwICpDATBgNVHSUEDDAKBggrBgEFBQcDATANBgkq
hkiG9w0BAQsFAAOCAQEANWeD4CGUYDPF4hz50VGSrXwtc8X8KP321RVvPm81dz5K
FtClRz2IaWrNrNgq8iNgQhz8sbZsiUnWLegEylx8gWxx1F4cRUUPyggNR8C3Ke1j
YwIGvTOH+tyk9o48cqJ2bO94mcggz0fPqAOrbagT02XRRKNEp/T+YaegxC1dyGUQ
G/HoBoXbjEjviJDHABHeL5pf1EafyUv1JAWw/62KWUL8pAfJYDORapVI1YRrDEVf
42MzonfrW1X3tZVvUenAWOqJ/dVjw3oCfzevXAuNL87TT+LHeh0wxd7TFXy+NJTK
rR7GdiO9Rc1JRuaBp2/Od2ROspsp2tQYZRAoG57aWw==
-----END CERTIFICATE-----)CERT";
// Used only if the mod was not updated yet: the relay, then bare HTTP.
constexpr auto AuthApiUrl = "https://berrytrip-auth-relay-6lacmy-preview.freebuff.site";
constexpr auto AuthApiDirectUrl = "http://135.181.126.167:25712";
constexpr auto LauncherVersion = "0.7.14";
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

QString Config::authApiDirect()
{
    return QString::fromLatin1(AuthApiDirectUrl);
}

QString Config::authApiTls()
{
    return QString::fromLatin1(AuthApiTlsUrl);
}

QByteArray Config::pinnedCertificate()
{
    return QByteArray::fromRawData(AuthApiPinnedPem, static_cast<int>(sizeof(AuthApiPinnedPem) - 1));
}

namespace {
/// BERRY_PACK_BASE lets the update path run against a local test pack; the
/// shipped build always uses the real one.
QString packBaseUrl()
{
    const QString custom = qEnvironmentVariable("BERRY_PACK_BASE");
    if (custom.isEmpty())
        return QString::fromLatin1(PackBaseUrl);
    return custom.endsWith(QLatin1Char('/')) ? custom : custom + QLatin1Char('/');
}
}  // namespace

QUrl Config::packIndexUrl()
{
    return QUrl(packBaseUrl() + QStringLiteral("index.toml"));
}

QUrl Config::packFileUrl(const QString& relativePath)
{
    // Packwiz paths are relative to the pack root and may contain spaces.
    const auto encoded = QString::fromLatin1(QUrl::toPercentEncoding(relativePath, QByteArrayLiteral("/")));
    return QUrl(packBaseUrl() + encoded);
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
