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

#include "ui_theme.hpp"
#include "fonts/fa_solid.hpp"
#include "fonts/roboto_medium.hpp"
#include <algorithm>

namespace
{

constexpr float UI_SCALE_MIN = 0.8f;
constexpr float UI_SCALE_MAX = 2.0f;

float s_ui_scale = 1.0f;

ImVec4 with_alpha(const ImVec4 &c, float alpha) { return {c.x, c.y, c.z, alpha}; }

void load_fonts()
{
    ImGuiIO &io = ImGui::GetIO();

    io.Fonts->AddFontFromMemoryCompressedBase85TTF(roboto_medium_compressed_data_base85, Theme::font_size_base);

    // Merged into the text font so icons can be used inline in any string.
    // The range must stay inside Font Awesome's private use area, otherwise the
    // merge would shadow Latin glyphs from Roboto.
    static const ImWchar icon_range[] = {0xf000, 0xf8ff, 0};
    ImFontConfig         icon_cfg;
    icon_cfg.MergeMode        = true;
    icon_cfg.GlyphMinAdvanceX = Theme::font_size_base;
    icon_cfg.GlyphOffset      = ImVec2(0.f, 1.f); // optical centring next to Roboto
    io.Fonts->AddFontFromMemoryCompressedBase85TTF(fa_solid_compressed_data_base85, Theme::font_size_base, &icon_cfg,
                                                   icon_range);
}

void apply_style()
{
    ImGui::StyleColorsDark();
    ImGuiStyle &style = ImGui::GetStyle();

    style.WindowRounding    = 8.f;
    style.ChildRounding     = 8.f;
    style.FrameRounding     = 5.f;
    style.PopupRounding     = 8.f;
    style.GrabRounding      = 5.f;
    style.ScrollbarRounding = 8.f;

    style.WindowPadding    = ImVec2(14, 12);
    style.FramePadding     = ImVec2(10, 6);
    style.ItemSpacing      = ImVec2(9, 7);
    style.ItemInnerSpacing = ImVec2(7, 5);
    style.CellPadding      = ImVec2(7, 5);
    style.ScrollbarSize    = 13.f;

    style.WindowBorderSize = 0.f;
    style.ChildBorderSize  = 1.f;
    style.FrameBorderSize  = 0.f;

    ImVec4 *c                        = style.Colors;
    c[ImGuiCol_WindowBg]             = Theme::background;
    c[ImGuiCol_ChildBg]              = Theme::surface;
    c[ImGuiCol_PopupBg]              = Theme::surface;
    c[ImGuiCol_Border]               = Theme::border;
    c[ImGuiCol_Text]                 = Theme::text;
    c[ImGuiCol_TextDisabled]         = Theme::text_dim;
    c[ImGuiCol_FrameBg]              = Theme::surface_raised;
    c[ImGuiCol_FrameBgHovered]       = Theme::surface_hover;
    c[ImGuiCol_FrameBgActive]        = Theme::surface_hover;
    c[ImGuiCol_TitleBg]              = Theme::surface;
    c[ImGuiCol_TitleBgActive]        = Theme::surface_raised;
    c[ImGuiCol_TitleBgCollapsed]     = Theme::surface;
    c[ImGuiCol_Button]               = Theme::surface_raised;
    c[ImGuiCol_ButtonHovered]        = Theme::surface_hover;
    c[ImGuiCol_ButtonActive]         = with_alpha(Theme::accent, 0.85f);
    c[ImGuiCol_Header]               = with_alpha(Theme::accent, 0.30f);
    c[ImGuiCol_HeaderHovered]        = with_alpha(Theme::accent, 0.45f);
    c[ImGuiCol_HeaderActive]         = with_alpha(Theme::accent, 0.60f);
    c[ImGuiCol_CheckMark]            = Theme::accent;
    c[ImGuiCol_SliderGrab]           = Theme::accent;
    c[ImGuiCol_SliderGrabActive]     = Theme::accent;
    c[ImGuiCol_Separator]            = Theme::border;
    c[ImGuiCol_SeparatorHovered]     = Theme::accent;
    c[ImGuiCol_SeparatorActive]      = Theme::accent;
    c[ImGuiCol_ResizeGrip]           = with_alpha(Theme::text_dim, 0.35f);
    c[ImGuiCol_ResizeGripHovered]    = Theme::accent;
    c[ImGuiCol_ResizeGripActive]     = Theme::accent;
    c[ImGuiCol_TableHeaderBg]        = Theme::surface_raised;
    c[ImGuiCol_TableBorderStrong]    = Theme::border;
    c[ImGuiCol_TableBorderLight]     = with_alpha(Theme::border, 0.5f);
    c[ImGuiCol_TableRowBg]           = ImVec4(0.f, 0.f, 0.f, 0.f);
    c[ImGuiCol_TableRowBgAlt]        = with_alpha(Theme::surface_raised, 0.45f);
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0.f, 0.f, 0.f, 0.f);
    c[ImGuiCol_ScrollbarGrab]        = Theme::surface_hover;
    c[ImGuiCol_ScrollbarGrabHovered] = Theme::border;
    c[ImGuiCol_ScrollbarGrabActive]  = Theme::accent;
}

} // namespace

ImVec4 Theme::rating_color(const std::string &rating)
{
    if (rating == "BUTTER!")
        return {1.00f, 1.00f, 0.00f, 1.0f};
    if (rating == "GREAT LANDING!")
        return {0.25f, 1.00f, 0.25f, 1.0f};
    if (rating == "ACCEPTABLE")
        return {0.00f, 0.80f, 0.00f, 1.0f};
    if (rating == "HARD LANDING!")
        return {1.00f, 0.50f, 0.00f, 1.0f};
    if (rating == "WASTED!")
        return {1.00f, 0.13f, 0.13f, 1.0f};
    return Theme::text;
}

void Theme::init()
{
    load_fonts();
    apply_style();
}

float Theme::ui_scale() { return s_ui_scale; }

void Theme::set_ui_scale(float scale)
{
    s_ui_scale = std::clamp(scale, UI_SCALE_MIN, UI_SCALE_MAX);

    // ScaleAllSizes() is not idempotent, so the style is rebuilt from scratch first.
    apply_style();
    if (s_ui_scale != 1.0f)
        ImGui::GetStyle().ScaleAllSizes(s_ui_scale);
    ImGui::GetStyle().FontScaleMain = s_ui_scale;
}
