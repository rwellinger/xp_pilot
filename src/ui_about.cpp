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

#include "ui_about.hpp"
#include <wellys_about.hpp>

namespace
{

#ifdef XP_PILOT_VERSION
constexpr const char *PLUGIN_VERSION = XP_PILOT_VERSION;
#else
constexpr const char *PLUGIN_VERSION = "SNAPSHOT";
#endif

// Mirrors section 1 of 3part-lizenz.md: what ships inside the plugin.
const WellysAbout::PluginInfo &plugin_info()
{
    static const WellysAbout::PluginInfo info{
        "xp_pilot",
        PLUGIN_VERSION,
        "Copyright (C) 2026 thWelly",
        "Free software under the GNU General Public License v3 or later, provided without any warranty.",
        {
            {"Dear ImGui", "MIT"},
            {"nlohmann/json", "MIT"},
            {"Roboto", "Apache-2.0"},
            {"Font Awesome Free", "SIL OFL 1.1, icons CC BY 4.0"},
            {"X-Plane SDK", "Laminar Research SDK License"},
            {"Natural Earth map data", "public domain"},
        },
    };
    return info;
}

} // namespace

void AboutScreen::draw() { WellysAbout::draw_content(plugin_info(), WellysAbout::photo_texture()); }

void AboutScreen::release() { WellysAbout::release_photo(); }
