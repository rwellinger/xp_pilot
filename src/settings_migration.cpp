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


#include "settings_migration.hpp"
#include <fstream>
#include <json.hpp>

namespace fs = std::filesystem;

namespace
{

// The old file is only worth moving if it is readable JSON. A truncated or
// hand-broken file is left where it is so the user can still recover it by hand.
bool is_readable_json(const fs::path &file)
{
    std::ifstream f(file);
    if (!f.is_open())
        return false;
    return !nlohmann::json::parse(f, nullptr, false).is_discarded();
}

} // namespace

Settings::MigrationOutcome Settings::migrate_settings_file(const fs::path &old_file, const fs::path &new_file)
{
    std::error_code ec;
    if (fs::exists(new_file, ec) || !fs::exists(old_file, ec))
        return MigrationOutcome::NothingToMigrate;

    if (!is_readable_json(old_file))
        return MigrationOutcome::OldFileMalformed;

    ec.clear();
    fs::create_directories(new_file.parent_path(), ec);

    // rename fails across volumes (X-Plane may sit on an external disk), so fall
    // back to copy + remove.
    ec.clear();
    fs::rename(old_file, new_file, ec);
    if (!ec)
        return MigrationOutcome::Migrated;

    ec.clear();
    fs::copy_file(old_file, new_file, fs::copy_options::overwrite_existing, ec);
    if (ec)
        return MigrationOutcome::MoveFailed;

    ec.clear();
    fs::remove(old_file, ec);
    return MigrationOutcome::Migrated;
}
