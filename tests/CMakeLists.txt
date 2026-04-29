# Standalone test executable. Links Catch2 amalgamated source directly.
# Never links the X-Plane SDK — domain-logic tests only. The same SDK-free
# translation units used by the plugin target are compiled in here so the
# tests exercise the production code, not a re-implementation.

set(CATCH2_DIR ${CMAKE_SOURCE_DIR}/vendor/catch2)

add_executable(xp_pilot_tests
    ${CATCH2_DIR}/catch_amalgamated.cpp
    ${CMAKE_SOURCE_DIR}/src/html_report.cpp
    test_smoke.cpp
    test_html_report.cpp
    test_auto_qnh_logic.cpp
)

target_include_directories(xp_pilot_tests PRIVATE
    ${CATCH2_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/vendor
)

# Path to the fixtures directory, made available to the test code.
target_compile_definitions(xp_pilot_tests PRIVATE
    XP_PILOT_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

if(MSVC)
    target_compile_options(xp_pilot_tests PRIVATE /W3)
    set_target_properties(xp_pilot_tests PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
else()
    target_compile_options(xp_pilot_tests PRIVATE -Wall -Wextra -Wno-unused-parameter)
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
