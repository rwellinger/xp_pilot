cmake_minimum_required(VERSION 3.21)
project(xp_pilot CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_TESTS "Build unit tests" ON)
option(XP_PILOT_SANITIZE "Enable ASan + UBSan on the unit tests" OFF)

if(XP_PILOT_SANITIZE)
    set(XP_PILOT_SANITIZER_FLAGS
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
        -fno-sanitize-recover=all
    )
    message(STATUS "Sanitizer build: ASan + UBSan on unit tests (.xpl plugin is not instrumented)")
endif()

if(APPLE)
    # Universal Binary: native arm64 + x86_64 via Rosetta.
    # Allow override from command line (e.g. make lint passes arm64 only).
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
    endif()
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0")
endif()

# ── Dear ImGui (downloaded by setup.sh into vendor/imgui) ─────────────────────
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/vendor/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl2.cpp
)

# ── Plugin target ──────────────────────────────────────────────────────────────
add_library(xp_pilot MODULE
    src/main.cpp
    src/auto_qnh.cpp
    src/airport_lookup.cpp
    src/flight_logger.cpp
    src/flight_store.cpp
    src/flight_store_migration.cpp
    src/html_report.cpp
    src/runway_data.cpp
    src/airspace_data.cpp
    src/map_overlay_cache.cpp
    src/coastline_data.cpp
    src/city_data.cpp
    src/logbook_ui.cpp
    src/ui_flight_list.cpp
    src/ui_flight_view.cpp
    src/ui_home.cpp
    src/settings.cpp
    src/settings_migration.cpp
    src/sim_clock.cpp
    src/ui_landing_popup.cpp
    src/ui_overlay.cpp
    src/ui_theme.cpp
    src/ui_widgets.cpp
    ${IMGUI_SOURCES}
)

# External deps as SYSTEM so compiler warnings and clang-tidy skip them.
# Own sources under src/ stay on the normal include path and remain linted.
target_include_directories(xp_pilot SYSTEM PRIVATE
    ${CMAKE_SOURCE_DIR}/sdk
    ${CMAKE_SOURCE_DIR}/sdk/XPLM
    ${CMAKE_SOURCE_DIR}/sdk/XPWidgets
    ${CMAKE_SOURCE_DIR}/vendor
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

file(READ "${CMAKE_SOURCE_DIR}/VERSION.txt" XP_PILOT_VERSION)
string(STRIP "${XP_PILOT_VERSION}" XP_PILOT_VERSION)

if(NOT RELEASE)
    set(XP_PILOT_VERSION "SNAPSHOT")
endif()

set_target_properties(xp_pilot PROPERTIES
    OUTPUT_NAME "xp_pilot"
    SUFFIX      ".xpl"
    PREFIX      ""
)

# ── Platform-specific compile definitions, flags, and libraries ────────────────
if(APPLE)
    target_compile_definitions(xp_pilot PRIVATE
        APL=1 IBM=0 LIN=0
        XPLM200=1 XPLM210=1 XPLM300=1 XPLM301=1 XPLM302=1 XPLM303=1
        XPLM400=1
        GL_SILENCE_DEPRECATION   # suppress macOS OpenGL deprecation warnings
        XP_PILOT_VERSION="${XP_PILOT_VERSION}"
    )
    target_compile_options(xp_pilot PRIVATE
        -Wall -Wextra -Wno-unused-parameter
        -fvisibility=hidden
    )
    # Use SDK stub frameworks for linking (X-Plane provides the real ones at runtime).
    set(SDK_MAC_LIBS "${CMAKE_SOURCE_DIR}/sdk/Libraries/Mac")
    target_link_libraries(xp_pilot PRIVATE
        "${SDK_MAC_LIBS}/XPLM.framework/Versions/Current/XPLM"
        "${SDK_MAC_LIBS}/XPWidgets.framework/Versions/Current/XPWidgets"
        "-framework OpenGL"
    )
elseif(UNIX)
    target_compile_definitions(xp_pilot PRIVATE
        APL=0 IBM=0 LIN=1
        XPLM200=1 XPLM210=1 XPLM300=1 XPLM301=1 XPLM302=1 XPLM303=1
        XPLM400=1
        XP_PILOT_VERSION="${XP_PILOT_VERSION}"
    )
    target_compile_options(xp_pilot PRIVATE
        -Wall -Wextra -Wno-unused-parameter
        -fvisibility=hidden
    )
    # On Linux, XPLM/XPWidgets are loaded at runtime by X-Plane — no import libs needed.
    # Link OpenGL for compile-time resolution.
    find_package(OpenGL REQUIRED)
    target_link_libraries(xp_pilot PRIVATE OpenGL::GL)
elseif(WIN32)
    target_compile_definitions(xp_pilot PRIVATE
        APL=0 IBM=1 LIN=0
        XPLM200=1 XPLM210=1 XPLM300=1 XPLM301=1 XPLM302=1 XPLM303=1
        XPLM400=1
        WIN32_LEAN_AND_MEAN NOMINMAX
        _CRT_SECURE_NO_WARNINGS
        _USE_MATH_DEFINES
        XP_PILOT_VERSION="${XP_PILOT_VERSION}"
    )
    target_compile_options(xp_pilot PRIVATE /W3)
    set_target_properties(xp_pilot PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    target_link_libraries(xp_pilot PRIVATE
        "${CMAKE_SOURCE_DIR}/sdk/Libraries/Win/XPLM_64.lib"
        "${CMAKE_SOURCE_DIR}/sdk/Libraries/Win/XPWidgets_64.lib"
        opengl32
    )
endif()

# ── Tests ─────────────────────────────────────────────────────────────────────
if(BUILD_TESTS)
    add_subdirectory(tests)
endif()
