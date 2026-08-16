#include "../src/flight_logger.hpp"
#include <catch_amalgamated.hpp>

TEST_CASE("popup_position: every enum value round-trips through its settings key")
{
    for (size_t i = 0; i < popup_position_keys().size(); ++i)
    {
        const auto position = static_cast<PopupPosition>(i);
        CHECK(popup_position_from_string(popup_position_to_string(position)) == position);
    }
}

TEST_CASE("popup_position: keys and labels stay in step with the enum")
{
    CHECK(popup_position_keys().size() == popup_position_labels().size());
    CHECK(std::string(popup_position_to_string(PopupPosition::TopCenter)) == "top_center");
    CHECK(std::string(popup_position_to_string(PopupPosition::BottomRight)) == "bottom_right");
    CHECK(popup_position_from_string("center") == PopupPosition::Center);
}

TEST_CASE("popup_position: unknown or empty settings values fall back to the default")
{
    CHECK(popup_position_from_string("") == POPUP_POSITION_DEFAULT);
    CHECK(popup_position_from_string("somewhere_else") == POPUP_POSITION_DEFAULT);
    CHECK(popup_position_from_string("TOP_CENTER") == POPUP_POSITION_DEFAULT);
}
