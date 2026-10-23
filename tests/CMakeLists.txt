# Forge IDE — C++ tests

find_package(GTest REQUIRED)

qt_add_executable(forge_tests
    unit/test_main.cpp
    unit/test_extensions.cpp
    unit/test_workspace.cpp
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

set_property(TARGET forge_tests PROPERTY CROSSCOMPILING_EMULATOR
    "${CMAKE_COMMAND};-E;env;PATH=$<TARGET_FILE_DIR:Qt6::Core>;QT_QPA_PLATFORM=offscreen"
)

gtest_discover_tests(forge_tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;QT_QPA_PLATFORM=set:offscreen"
)

add_test(NAME Startup.ResourcesAndScreenCapture
    COMMAND forge_app --no-splash --theme Graphite --screenshot ${CMAKE_CURRENT_BINARY_DIR}/startup.png)
set_tests_properties(Startup.ResourcesAndScreenCapture PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    TIMEOUT 20)
target_compile_definitions(forge_tests PRIVATE FORGE_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
