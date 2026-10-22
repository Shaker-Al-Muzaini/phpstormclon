qt_add_executable(forge_app
    main.cpp
    MainWindow.cpp
    MainWindow.h
)

set_target_properties(forge_app PROPERTIES
    WIN32_EXECUTABLE ON
    OUTPUT_NAME "ForgeIDE"
)

target_link_libraries(forge_app
    PRIVATE
        forge_core
        forge_ui
        Qt6::Core
        Qt6::Widgets
        Qt6::Gui
)

target_include_directories(forge_app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/core/include
    ${CMAKE_SOURCE_DIR}/ui/include
)
