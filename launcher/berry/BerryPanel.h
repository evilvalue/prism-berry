// SPDX-License-Identifier: GPL-3.0-only
/*
 *  BerryTrip Launcher - single server build of Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QVector>
#include <QWidget>

class QButtonGroup;
class QCheckBox;
class QFrame;
class QLabel;
class QLineEdit;
class QListWidget;
class QMainWindow;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTimer;
class MinecraftInstance;

namespace berry {

class PackSync;
class ServerPing;
class Updater;
class Skin3D;
class TelegramAuth;

/// BerryUp's five screens rebuilt on Qt with the pixel theme:
/// Home, Account, Settings, Diagnostics and About.
class Panel : public QWidget {
    Q_OBJECT

   public:
    explicit Panel(QWidget* parent = nullptr);

    /// Shows a short message in the panel instead of opening a Prism dialog.
    void notice(const QString& text);

   private:
    QWidget* buildHome();
    QWidget* buildAccount();
    QWidget* buildSkins();
    QWidget* buildSettings();
    QWidget* buildDiagnostics();
    QWidget* buildAbout();
    QWidget* page(QWidget* content);

    void refreshAuth();
    void refreshLaunch();
    void refreshDiagnostics();
    void startSync();
    void startPing();
    void onPlay();
    void onAuthClicked();
    void onMemoryChanged(int gigabytes);
    void onJavaEdited();
    void applyTweaks(const QString& note);
    void checkUpdates();
    void installUpdate();
    bool signedIn() const;
    void goToPage(int index);

    void refreshSkins();
    void pickSkin();
    void pickCape();
    void applySelectedSkin();
    void refreshSkinPreview(const QString& path);
    QString modelValue() const;

    MinecraftInstance* m_instance = nullptr;
    PackSync* m_sync = nullptr;
    TelegramAuth* m_auth = nullptr;
    ServerPing* m_ping = nullptr;
    QTimer* m_pollTimer = nullptr;
    QStackedWidget* m_pages = nullptr;
    QButtonGroup* m_nav = nullptr;
    bool m_syncFinished = false;
    bool m_loading = false;
    QString m_statusBase;
    int m_busyFrame = 0;

    QLabel* m_chip = nullptr;
    QLabel* m_motd = nullptr;
    QLabel* m_players = nullptr;
    QLabel* m_pingLabel = nullptr;
    QLabel* m_homeAccount = nullptr;
    QProgressBar* m_progress = nullptr;
    QLabel* m_status = nullptr;
    QPushButton* m_play = nullptr;
    QPushButton* m_retry = nullptr;

    QLineEdit* m_nick = nullptr;
    QLabel* m_authStatus = nullptr;
    QPushButton* m_login = nullptr;
    QPushButton* m_openTelegram = nullptr;
    QPushButton* m_logout = nullptr;

    QSpinBox* m_memory = nullptr;
    QLineEdit* m_javaPath = nullptr;
    QLabel* m_settingsStatus = nullptr;
    QCheckBox* m_packs = nullptr;
    QCheckBox* m_shaders = nullptr;
    QVector<QCheckBox*> m_modChecks;

    Skin3D* m_skinPreview = nullptr;
    QLabel* m_skinName = nullptr;
    QLabel* m_skinStatus = nullptr;
    QLabel* m_capeName = nullptr;
    QListWidget* m_skinList = nullptr;
    QListWidget* m_capeList = nullptr;
    QPushButton* m_modelAuto = nullptr;
    QPushButton* m_modelClassic = nullptr;
    QPushButton* m_modelSlim = nullptr;
    QString m_pickedSkin;

    QVector<QLabel*> m_diagValues;
    QLabel* m_aboutStatus = nullptr;

    Updater* m_updater = nullptr;
    QFrame* m_updateBanner = nullptr;
    QLabel* m_updateText = nullptr;
    QPushButton* m_updateButton = nullptr;
    QProgressBar* m_updateProgress = nullptr;
    QPushButton* m_aboutUpdate = nullptr;
    QString m_updateVersion;
    QString m_updatePageUrl;
    bool m_updateReady = false;
    bool m_updateCanAuto = false;
};

/// Hides Prism's instance manager and puts the BerryTrip UI in its place.
void installSoloUi(QMainWindow* window);

}  // namespace berry
