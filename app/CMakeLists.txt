add_library(forge_workspace STATIC
    MainWindow.cpp
    MainWindow.h
    MainWindowShell.cpp
    MainWindowActions.cpp
    MainWindowFiles.cpp
    MainWindowTerminal.cpp
    TerminalView.h
    TerminalView.cpp
    PhpEditorController.h
    PhpEditorController.cpp
    GitPanel.h
    GitPanel.cpp
    DatabasePanel.h
    DatabasePanel.cpp
    InspectionPanel.h
    InspectionPanel.cpp
    MainWindowDebug.cpp
    MainWindowSearch.cpp
    MainWindowAppearance.cpp
    MainWindowFeatures.cpp
    WorkspaceSupport.cpp
    EditorWidgets.cpp
    FindBar.h
    FindBar.cpp
    ExtensionsDialog.h
    ExtensionsDialog.cpp
    ProblemsPanel.h
    ProblemsPanel.cpp
    MainWindowDiagnostics.cpp
    MainWindowIntelligence.cpp
    BrandSplash.cpp
)

target_link_libraries(forge_workspace PUBLIC forge_core forge_ui Qt6::Core Qt6::Widgets Qt6::Gui)
target_include_directories(forge_workspace PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

qt_add_executable(forge_app main.cpp ${CMAKE_SOURCE_DIR}/resources/resources.qrc)
set_target_properties(forge_app PROPERTIES AUTORCC ON WIN32_EXECUTABLE ON OUTPUT_NAME "ForgeIDE")
target_link_libraries(forge_app PRIVATE forge_workspace)
if(FORGE_BUILD_RUST)
    add_dependencies(forge_app phpintel dbtools)
    add_custom_command(TARGET forge_app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:phpintel> $<TARGET_FILE_DIR:forge_app>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:dbtools> $<TARGET_FILE_DIR:forge_app>)
endif()
