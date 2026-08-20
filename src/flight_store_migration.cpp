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

#include "flight_store_migration.hpp"
#include <XPLM/XPLMUtilities.h>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace
{

// Move one file, falling back to copy + remove when source and destination are on
// different volumes (rename then fails with cross_device_link). Soft-fails.
void move_file(const fs::path &src, const fs::path &dst)
{
    std::error_code ec;
    if (!fs::exists(src, ec))
        return;
    fs::create_directories(dst.parent_path(), ec);
    ec.clear();
    fs::rename(src, dst, ec);
    if (!ec)
        return;
    ec.clear();
    fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    if (ec)
    {
        XPLMDebugString(
            ("[xp_pilot] migrate: cannot move " + src.filename().string() + ": " + ec.message() + "\n").c_str());
        return;
    }
    ec.clear();
    fs::remove(src, ec);
}

void move_matching(const fs::path &src_dir, const fs::path &dst_dir, const std::string &ext)
{
    std::error_code ec;
    auto            it = fs::directory_iterator(src_dir, ec);
    if (ec)
        return;
    for (auto &entry : it)
    {
        if (!entry.is_regular_file())
            continue;
        const std::string name = entry.path().filename().string();
        if (name.size() > ext.size() && name.compare(name.size() - ext.size(), ext.size(), ext) == 0)
            move_file(entry.path(), dst_dir / name);
    }
}

} // namespace

void FlightStore::migrate_user_data_to_output(const fs::path &config_dir, const fs::path &output_dir)
{
    std::error_code ec;
    const fs::path  marker = output_dir / ".migrated";
    if (fs::exists(marker, ec))
        return;

    move_matching(config_dir / "flights", output_dir / "flights", ".json");
    move_matching(config_dir / "flights" / "archived", output_dir / "flights" / "archived", ".json");
    move_matching(config_dir / "reports", output_dir / "reports", ".html");
    move_matching(config_dir / "reports" / "archived", output_dir / "reports" / "archived", ".html");
    move_file(config_dir / "index.html", output_dir / "index.html");
    move_file(config_dir / "settings.json", output_dir / "settings.json");

    std::ofstream(marker) << "migrated\n";
}
