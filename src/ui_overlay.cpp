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

#include "ui_overlay.hpp"
#include "sim_clock.hpp"
#include <XPLM/XPLMDisplay.h>
#include <XPLM/XPLMGraphics.h>

namespace
{

std::string s_text;
double      s_until = 0;
float       s_red = 1, s_green = 1, s_blue = 1;
bool        s_enabled = true;

} // namespace

void Overlay::set_enabled(bool on) { s_enabled = on; }
bool Overlay::enabled() { return s_enabled; }

void Overlay::show(const std::string &text, float seconds, float red, float green, float blue)
{
    if (!s_enabled)
        return;
    s_text  = text;
    s_until = SimClock::seconds() + seconds;
    s_red   = red;
    s_green = green;
    s_blue  = blue;
}

void Overlay::draw()
{
    if (s_text.empty())
        return;
    if (SimClock::seconds() > s_until)
    {
        s_text.clear();
        return;
    }

    int screen_w = 0, screen_h = 0;
    XPLMGetScreenSize(&screen_w, &screen_h);

    XPLMSetGraphicsState(0, 0, 0, 1, 1, 0, 0);
    float     color[4] = {s_red, s_green, s_blue, 1.0f};
    const int x        = screen_w / 2 - 150;
    const int y        = static_cast<int>(static_cast<float>(screen_h) * 0.12f);
    XPLMDrawString(color, x, y, const_cast<char *>(s_text.c_str()), nullptr, xplmFont_Proportional);
}
