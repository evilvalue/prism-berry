// SPDX-License-Identifier: GPL-3.0-only
/*
 *  BerryTrip Launcher - single server build of Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QMainWindow;
class QProgressBar;
class QPushButton;
class QSpinBox;
class MinecraftInstance;

namespace berry {

class PackSync;

/// The whole user interface of the fork: account, memory, Java and a Play button.
class Panel : public QWidget {
    Q_OBJECT

   public:
    explicit Panel(QWidget* parent = nullptr);

   private:
    void buildUi();
    void loadSettings();
    void refreshAccount();
    void refreshLaunchButton();
    void startSync();
    void onPlayClicked();
    void onBrowseJava();
    void onJavaEdited();
    void onMemoryChanged(int gigabytes);

    MinecraftInstance* m_instance = nullptr;
    PackSync* m_sync = nullptr;
    bool m_syncFinished = false;

    QLabel* m_accountLabel = nullptr;
    QSpinBox* m_memory = nullptr;
    QLineEdit* m_javaPath = nullptr;
    QProgressBar* m_progress = nullptr;
    QLabel* m_status = nullptr;
    QPushButton* m_play = nullptr;
    QPushButton* m_retry = nullptr;
};

/// Hides Prism's instance manager and puts the single-server panel in its place.
void installSoloUi(QMainWindow* window);

}  // namespace berry
