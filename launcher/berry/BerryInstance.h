// SPDX-License-Identifier: GPL-3.0-only
/*
 *  BerryTrip Launcher - single server build of Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QString>

class MinecraftInstance;

namespace berry {

/// Creates the one fixed instance (instances/BerryTrip) on first run.
/// The pack files themselves are installed by PackSync, not by this class.
class InstanceSetup {
   public:
    /// Returns the instance, creating instance.cfg and mmc-pack.json if needed.
    /// Returns nullptr and fills `error` when the instance cannot be created.
    static MinecraftInstance* ensure(QString* error = nullptr);

    /// Looks the instance up in the launcher's instance list.
    static MinecraftInstance* find();

   private:
    static bool writeInstanceFiles(const QString& root, QString* error);
};

}  // namespace berry
