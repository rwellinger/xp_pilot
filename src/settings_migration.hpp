/*
 * xp_pilot - Flight Logger and Auto QNH plugin for X-Plane 12
 * Copyright (C) 2026 thWelly
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */


#pragma once
#include <filesystem>

namespace Settings
{

// What happened to the settings file on start-up. The caller turns this into a
// Log.txt line; the migration itself stays free of the X-Plane SDK so it can be
// tested.
enum class MigrationOutcome
{
    NothingToMigrate,   // no old file, or the new one already exists
    Migrated,           // old file moved to the new location
    OldFileMalformed,   // old file is not readable JSON — left untouched
    MoveFailed,         // old file is fine but could not be written to the new path
};

// Move the settings file from its old location to the new one, once. Never
// destroys data: an existing new file wins and the old one is left in place, and
// so is an old file that does not parse.
MigrationOutcome migrate_settings_file(const std::filesystem::path &old_file, const std::filesystem::path &new_file);

} // namespace Settings
