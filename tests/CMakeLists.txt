# Forge IDE — C++ tests

find_package(GTest REQUIRED)

qt_add_executable(forge_tests
    unit/test_main.cpp
    unit/test_extensions.cpp
    unit/test_workspace.cpp
    unit/test_language_client.cpp
    unit/test_terminal.cpp
    unit/test_tooling.cpp
    ${CMAKE_SOURCE_DIR}/resources/resources.qrc
)
set_property(TARGET forge_tests PROPERTY AUTORCC ON)

target_link_libraries(forge_tests PRIVATE
    forge_core
    forge_workspace
    Qt6::Test
    forge_platform
    GTest::gtest
    Qt6::Core
    Qt6::Widgets
)

# The Windows offscreen plugin uses FreeType instead of the native font database.
find_package(Qt6QOffscreenIntegrationPlugin QUIET CONFIG HINTS "${Qt6Gui_DIR}")
if(TARGET Qt6::QOffscreenIntegrationPlugin)
    set(forge_qt_plugin_dir "$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>/..")
else()
    cmake_path(SET forge_qt_plugin_dir NORMALIZE "${Qt6_DIR}/../../../plugins")
endif()
set_property(TARGET forge_tests PROPERTY CROSSCOMPILING_EMULATOR
    "${CMAKE_COMMAND};-E;env;QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=${forge_qt_plugin_dir}")
set(forge_test_environment
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    "QT_PLUGIN_PATH=set:${forge_qt_plugin_dir}"
    "QT_QPA_PLATFORM=set:offscreen")
if(WIN32)
    file(TO_CMAKE_PATH "$ENV{WINDIR}/Fonts" forge_test_font_dir)
    list(APPEND forge_test_environment "QT_QPA_FONTDIR=set:${forge_test_font_dir}")
endif()

gtest_discover_tests(forge_tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES ENVIRONMENT_MODIFICATION
        "${forge_test_environment}"
)

add_test(NAME Startup.ResourcesAndScreenCapture
    COMMAND forge_app --no-splash --theme Graphite --screenshot ${CMAKE_CURRENT_BINARY_DIR}/startup.png)
set_tests_properties(Startup.ResourcesAndScreenCapture PROPERTIES
    ENVIRONMENT_MODIFICATION "${forge_test_environment}"
    TIMEOUT 20)
if(FORGE_BUILD_RUST)
    add_dependencies(forge_tests dbtools)
    add_custom_command(TARGET forge_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:dbtools> $<TARGET_FILE_DIR:forge_tests>)
endif()
target_compile_definitions(forge_tests PRIVATE FORGE_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
