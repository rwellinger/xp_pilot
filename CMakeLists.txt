cmake_minimum_required(VERSION 3.21)
project(xp_pilot CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Universal Binary: native arm64 + x86_64 via Rosetta
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0")

# ── X-Plane SDK headers ────────────────────────────────────────────────────────
# Download once with: ./setup.sh
include_directories(
    ${CMAKE_SOURCE_DIR}/sdk
    ${CMAKE_SOURCE_DIR}/sdk/XPLM
    ${CMAKE_SOURCE_DIR}/sdk/XPWidgets
    ${CMAKE_SOURCE_DIR}/vendor
)

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
    src/rain_blocker.cpp
    src/auto_qnh.cpp
    src/flight_logger.cpp
    src/html_report.cpp
    src/logbook_ui.cpp
    ${IMGUI_SOURCES}
)

target_include_directories(xp_pilot PRIVATE
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

set_target_properties(xp_pilot PROPERTIES
    OUTPUT_NAME "xp_pilot"
    SUFFIX      ".xpl"
    PREFIX      ""
)

target_compile_definitions(xp_pilot PRIVATE
    APL=1 IBM=0 LIN=0    # macOS platform
    XPLM200=1 XPLM210=1 XPLM300=1 XPLM301=1 XPLM302=1 XPLM303=1
    XPLM400=1
    GL_SILENCE_DEPRECATION   # suppress macOS OpenGL deprecation warnings
)

target_compile_options(xp_pilot PRIVATE
    -Wall -Wextra -Wno-unused-parameter
    -fvisibility=hidden
)

# ── Link against X-Plane frameworks (already in X-Plane installation) ──────────
# Link directly to the framework binaries to avoid -F flag quoting issues with spaces in path.
set(XPLANE_PLUGINS_DIR "/Users/robertw/X-Plane 12/Resources/plugins")
target_link_libraries(xp_pilot PRIVATE
    "${XPLANE_PLUGINS_DIR}/XPLM.framework/Versions/Current/XPLM"
    "${XPLANE_PLUGINS_DIR}/XPWidgets.framework/Versions/Current/XPWidgets"
    "-framework OpenGL"
)
