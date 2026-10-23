# Forge IDE — C++ tests

find_package(GTest REQUIRED)

qt_add_executable(forge_tests
    unit/test_main.cpp
    ${CMAKE_SOURCE_DIR}/resources/resources.qrc
)
set_property(TARGET forge_tests PROPERTY AUTORCC ON)

target_link_libraries(forge_tests PRIVATE
    forge_core
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
