// Headless render smoke test for the tablet UI.
//
// Dear ImGui is driven without a renderer backend: the font atlas is built and
// handed a dummy texture id, then a full frame is assembled. This catches the two
// failures that would otherwise only show up inside X-Plane — a font atlas that
// does not bake, and an icon define whose codepoint is missing from the subset.

#include "catch_amalgamated.hpp"
#include "html_report.hpp"
#include "ui_flight_view.hpp"
#include "ui_home.hpp"
#include "ui_theme.hpp"
#include "ui_widgets.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <imgui.h>
#include <ios>
#include <iterator>
#include <string>

namespace
{

struct ImGuiHeadlessContext
{
    ImGuiHeadlessContext()
    {
        IMGUI_CHECKVERSION();
        context = ImGui::CreateContext();
        ImGui::SetCurrentContext(context);

        ImGuiIO &io    = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.DisplaySize = ImVec2(1060.f, 720.f);
        io.DeltaTime   = 1.f / 60.f;

        Theme::init();

        // Stand in for the renderer backend: bake the atlas and give it a texture id.
        unsigned char *pixels = nullptr;
        int            width = 0, height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        REQUIRE(pixels != nullptr);
        REQUIRE(width > 0);
        REQUIRE(height > 0);
        io.Fonts->SetTexID(static_cast<ImTextureID>(1));
    }

    ~ImGuiHeadlessContext() { ImGui::DestroyContext(context); }

    ImGuiContext *context = nullptr;
};

// All Font Awesome icons sit in the private use area, i.e. three UTF-8 bytes.
unsigned int codepoint_of(const char *utf8_icon)
{
    const auto *bytes = reinterpret_cast<const unsigned char *>(utf8_icon);
    return static_cast<unsigned int>((bytes[0] & 0x0F) << 12 | (bytes[1] & 0x3F) << 6 | (bytes[2] & 0x3F));
}

// A window is skipped on its very first frame while ImGui settles its auto-size,
// so the frame under test is always the second one.
template <typename DrawBody> const ImDrawData *render_frames(DrawBody &&body)
{
    for (int frame = 0; frame < 2; ++frame)
    {
        ImGui::NewFrame();
        ImGui::SetNextWindowSize(ImVec2(1000.f, 700.f));
        ImGui::Begin("test");
        body();
        ImGui::End();
        ImGui::Render();
    }
    return ImGui::GetDrawData();
}

FlightData load_fixture(const char *name)
{
    std::ifstream file(std::string(XP_PILOT_TEST_FIXTURES_DIR) + "/" + name);
    REQUIRE(file.is_open());
    std::string content((std::istreambuf_iterator<char>(file)), {});
    return parse_flight_json(content, name);
}

} // namespace

// The regression these guard: a UI scale of 2.0 sized the logbook window at 2120x1440.
// On any narrower screen its title bar and resize grip landed off-screen, so the window
// could neither be moved nor shrunk — and the scale slider that caused it sat inside that
// unreachable window. Recovery meant hand-editing settings.json.
TEST_CASE("UI scale is clamped to a usable range", "[ui][scale]")
{
    ImGuiHeadlessContext ctx;

    Theme::set_ui_scale(99.f);
    REQUIRE(Theme::ui_scale() == Catch::Approx(Theme::ui_scale_max));

    Theme::set_ui_scale(0.f);
    REQUIRE(Theme::ui_scale() == Catch::Approx(Theme::ui_scale_min));

    Theme::set_ui_scale(-5.f);
    REQUIRE(Theme::ui_scale() == Catch::Approx(Theme::ui_scale_min));

    // A value from settings.json takes the same path, so a hand-edited file cannot
    // reintroduce an unusable scale.
    Theme::set_ui_scale(1.25f);
    REQUIRE(Theme::ui_scale() == Catch::Approx(1.25f));

    Theme::reset_ui_scale();
    REQUIRE(Theme::ui_scale() == Catch::Approx(Theme::ui_scale_default));
}

TEST_CASE("UI scale steps stay on the grid and inside the range", "[ui][scale]")
{
    // 5% steps, so setups that need fine tuning — 85% on a TV-distance monitor — are
    // reachable and survive a click on either button.
    REQUIRE(Theme::stepped_ui_scale(0.85f, +1) == Catch::Approx(0.90f));
    REQUIRE(Theme::stepped_ui_scale(0.90f, -1) == Catch::Approx(0.85f));
    REQUIRE(Theme::stepped_ui_scale(1.0f, +1) == Catch::Approx(1.05f));
    REQUIRE(Theme::stepped_ui_scale(1.0f, -1) == Catch::Approx(0.95f));

    // Both ends are hard stops, so repeated clicking cannot escape the range.
    REQUIRE(Theme::stepped_ui_scale(Theme::ui_scale_max, +1) == Catch::Approx(Theme::ui_scale_max));
    REQUIRE(Theme::stepped_ui_scale(Theme::ui_scale_min, -1) == Catch::Approx(Theme::ui_scale_min));

    // A value from a hand-edited settings.json is snapped back onto the grid.
    REQUIRE(Theme::stepped_ui_scale(1.234f, +1) == Catch::Approx(1.30f));
    REQUIRE(Theme::stepped_ui_scale(1.234f, -1) == Catch::Approx(1.20f));

    // Every step from bottom to top lands on a round percentage, and the grid reaches
    // the maximum exactly rather than stopping short of it.
    float scale = Theme::ui_scale_min;
    for (int i = 0; i < 24; ++i)
    {
        scale                  = Theme::stepped_ui_scale(scale, +1);
        const float percentage = scale * 100.f;
        REQUIRE(percentage == Catch::Approx(std::round(percentage)).margin(0.01f));
    }
    REQUIRE(scale == Catch::Approx(Theme::ui_scale_max));

    // And back down again, symmetrically.
    for (int i = 0; i < 24; ++i)
        scale = Theme::stepped_ui_scale(scale, -1);
    REQUIRE(scale == Catch::Approx(Theme::ui_scale_min));
}

TEST_CASE("the logbook window never exceeds the screen at maximum scale", "[ui][scale]")
{
    ImGuiHeadlessContext ctx;

    // A modest laptop screen — the case that broke.
    constexpr float screen_w = 1440.f;
    constexpr float screen_h = 900.f;

    Theme::set_ui_scale(Theme::ui_scale_max);
    REQUIRE(Theme::scaled(1060.f) > screen_w); // unclamped this is what overflowed

    const Theme::WindowFit fit =
        Theme::fit_window_to_screen(Theme::scaled(1060.f), Theme::scaled(720.f), screen_w, screen_h);

    REQUIRE(fit.size.x <= screen_w);
    REQUIRE(fit.size.y <= screen_h);
    REQUIRE(fit.max_size.x <= screen_w);
    REQUIRE(fit.max_size.y <= screen_h);

    // Fully on screen, so the title bar and resize grip stay grabbable.
    REQUIRE(fit.pos.x >= 0.f);
    REQUIRE(fit.pos.y >= 0.f);
    REQUIRE(fit.pos.x + fit.size.x <= screen_w);
    REQUIRE(fit.pos.y + fit.size.y <= screen_h);

    Theme::reset_ui_scale();
}

TEST_CASE("a window smaller than the screen keeps its requested size", "[ui][scale]")
{
    ImGuiHeadlessContext ctx;
    Theme::reset_ui_scale();

    const Theme::WindowFit fit = Theme::fit_window_to_screen(1060.f, 720.f, 2560.f, 1440.f);

    REQUIRE(fit.size.x == Catch::Approx(1060.f));
    REQUIRE(fit.size.y == Catch::Approx(720.f));
    REQUIRE(fit.pos.x == Catch::Approx((2560.f - 1060.f) * 0.5f));
}

TEST_CASE("every icon define resolves to a glyph in the embedded subset", "[ui]")
{
    ImGuiHeadlessContext ctx;

    // Every icon the UI can draw. A missing entry here means the codepoint was not
    // included in tools/generate_fonts.sh, or the UTF-8 escape in ui_theme.hpp is wrong.
    const char *icons[] = {ICON_FA_CHECK,        ICON_FA_XMARK,      ICON_FA_GEAR,     ICON_FA_ROTATE, ICON_FA_BOOK,
                           ICON_FA_CHEVRON_LEFT, ICON_FA_ARROW_LEFT, ICON_FA_WARNING,  ICON_FA_SQUARE, ICON_FA_CIRCLE,
                           ICON_FA_SQUARE_CHECK, ICON_FA_FILE_LINES, ICON_FA_ARCHIVE,  ICON_FA_TRASH,  ICON_FA_MAP,
                           ICON_FA_STOPWATCH,    ICON_FA_EXTERNAL,   ICON_FA_PLANE_DEP};

    ImFont      *font  = ImGui::GetIO().Fonts->Fonts[0];
    ImFontBaked *baked = font->GetFontBaked(Theme::font_size_base);
    REQUIRE(baked != nullptr);

    for (const char *icon : icons)
    {
        const unsigned int codepoint = codepoint_of(icon);
        INFO("icon codepoint U+" << std::hex << codepoint);
        CHECK(baked->FindGlyphNoFallback(static_cast<ImWchar>(codepoint)) != nullptr);
    }
}

TEST_CASE("a full home screen frame renders", "[ui]")
{
    ImGuiHeadlessContext ctx;

    FlightLogger::LiveFlight live;
    live.in_progress            = true;
    live.flight                 = load_fixture("sample_flight.json");
    live.altitude_ft            = 5400;
    live.indicated_airspeed_kts = 118;
    live.vertical_speed_fpm     = 420;
    live.latitude               = 47.458;
    live.longitude              = 8.548;

    Home::Screen      screen    = Home::Screen::Home;
    const ImDrawData *draw_data = render_frames(
        [&]
        {
            Home::draw_status_bar(live);
            Ui::begin_content_panel("home_panel");
            Home::draw_tiles(live, {12, 3, true}, screen);
            Ui::end_content_panel();
        });

    REQUIRE(draw_data != nullptr);
    CHECK(draw_data->Valid);
    CHECK(draw_data->TotalVtxCount > 0);
    CHECK(screen == Home::Screen::Home); // nothing clicked, nothing navigated
}

TEST_CASE("a flight detail frame renders", "[ui]")
{
    ImGuiHeadlessContext ctx;

    const FlightData flight = load_fixture("sample_flight.json");

    const ImDrawData *draw_data = render_frames(
        [&]
        {
            CHECK_FALSE(Ui::view_header(ICON_FA_BOOK, "LOGBOOK"));
            FlightView::draw_detail(flight, 600.f);
        });

    CHECK(draw_data->TotalVtxCount > 0);
}

TEST_CASE("the status bar renders without a flight in progress", "[ui]")
{
    ImGuiHeadlessContext ctx;

    const ImDrawData *draw_data = render_frames([] { Home::draw_status_bar(FlightLogger::LiveFlight{}); });

    CHECK(draw_data->TotalVtxCount > 0);
}

TEST_CASE("the home grid fits its panel without scrolling", "[ui]")
{
    ImGuiHeadlessContext ctx;

    // The tiles are sized from the panel's available height, so an off-by-a-few-pixels
    // layout would leave the panel permanently scrollable.
    Home::Screen screen     = Home::Screen::Home;
    float        scroll_max = -1.f;

    render_frames(
        [&]
        {
            Home::draw_status_bar(FlightLogger::LiveFlight{});
            Ui::begin_content_panel("home_panel");
            Home::draw_tiles(FlightLogger::LiveFlight{}, {12, 3, true}, screen);
            scroll_max = ImGui::GetScrollMaxY();
            Ui::end_content_panel();
        });

    CHECK(scroll_max == 0.f);
}

TEST_CASE("the time row always shows three cells", "[ui]")
{
    // The regression: an unpaused flight used to render a single BLOCK TIME cell and
    // grew two more the moment paused_sec went above zero. With a phantom one-second
    // pause appearing every second, that made the live view flicker.
    FlightData flight;
    flight.block_time_min = 47;
    flight.block_time_sec = 2820;

    flight.paused_sec                 = 0;
    const FlightView::TimeCells clean = FlightView::time_cells(flight);
    CHECK_FALSE(clean.was_paused);
    CHECK(clean.paused == "--");
    CHECK(clean.total == clean.block); // nothing to subtract
    CHECK_FALSE(clean.block.empty());

    flight.paused_sec                  = 1200;
    const FlightView::TimeCells paused = FlightView::time_cells(flight);
    CHECK(paused.was_paused);
    CHECK(paused.paused == "20m 00s");
    CHECK(paused.block == "47m 00s");
    CHECK(paused.total == "1h 07m"); // block + pause, and the three add up
}

TEST_CASE("the time row keeps its height whether or not the flight was paused", "[ui]")
{
    ImGuiHeadlessContext ctx;

    // A running flight crossing into a pause must not shift everything below it.
    auto row_height = [](int paused_sec)
    {
        FlightData flight;
        flight.block_time_min = 47;
        flight.block_time_sec = 2820;
        flight.paused_sec     = paused_sec;

        float height = -1.f;
        render_frames(
            [&]
            {
                const float before = ImGui::GetCursorPosY();
                FlightView::draw_time_lines(flight);
                height = ImGui::GetCursorPosY() - before;
            });
        return height;
    };

    const float without_pause = row_height(0);
    const float with_pause    = row_height(1200);

    CHECK(without_pause > 0.f);
    CHECK(without_pause == with_pause);
}
