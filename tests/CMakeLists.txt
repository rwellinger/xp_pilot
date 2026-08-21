# Standalone test executable. Links Catch2 amalgamated source directly.
# Never links the X-Plane SDK — domain-logic tests only. The same SDK-free
# translation units used by the plugin target are compiled in here so the
# tests exercise the production code, not a re-implementation.

set(CATCH2_DIR ${CMAKE_SOURCE_DIR}/vendor/catch2)

# Dear ImGui without a backend, so the UI modules can be exercised headless.
set(IMGUI_TEST_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
)

add_executable(xp_pilot_tests
    ${CATCH2_DIR}/catch_amalgamated.cpp
    ${CMAKE_SOURCE_DIR}/src/html_report.cpp
    ${CMAKE_SOURCE_DIR}/src/runway_data.cpp
    ${CMAKE_SOURCE_DIR}/src/airspace_data.cpp
    ${CMAKE_SOURCE_DIR}/src/flight_store.cpp
    ${CMAKE_SOURCE_DIR}/src/map_overlay_cache.cpp
    ${CMAKE_SOURCE_DIR}/src/coastline_data.cpp
    ${CMAKE_SOURCE_DIR}/src/city_data.cpp
    ${CMAKE_SOURCE_DIR}/src/ui_theme.cpp
    ${CMAKE_SOURCE_DIR}/src/ui_widgets.cpp
    ${CMAKE_SOURCE_DIR}/src/ui_flight_view.cpp
    ${CMAKE_SOURCE_DIR}/src/ui_home.cpp
    ${IMGUI_TEST_SOURCES}
    test_smoke.cpp
    test_html_report.cpp
    test_auto_qnh_logic.cpp
    test_flight_logger_logic.cpp
    test_runway.cpp
    test_airspace.cpp
    test_popup_position.cpp
    test_flight_store.cpp
    test_ui_render.cpp
)

target_include_directories(xp_pilot_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_include_directories(xp_pilot_tests SYSTEM PRIVATE
    ${CATCH2_DIR}
    ${CMAKE_SOURCE_DIR}/vendor
    ${IMGUI_DIR}
)

# Path to the fixtures directory, made available to the test code.
target_compile_definitions(xp_pilot_tests PRIVATE
    XP_PILOT_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    XP_PILOT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

if(MSVC)
    target_compile_options(xp_pilot_tests PRIVATE /W3)
    set_target_properties(xp_pilot_tests PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
else()
    target_compile_options(xp_pilot_tests PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

if(XP_PILOT_SANITIZE)
    target_compile_options(xp_pilot_tests PRIVATE
        ${XP_PILOT_SANITIZER_FLAGS} -O1 -g
    )
    target_link_options(xp_pilot_tests PRIVATE
        ${XP_PILOT_SANITIZER_FLAGS}
    )
endif()

# Tests run on the host arch only — no Universal Binary, no Rosetta slice.
if(APPLE)
    set_target_properties(xp_pilot_tests PROPERTIES
        OSX_ARCHITECTURES "${CMAKE_HOST_SYSTEM_PROCESSOR}"
    )
endif()

set_target_properties(xp_pilot_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
