add_library(forge_core STATIC
    include/forge/core/ActionManager.h
    include/forge/core/ExtensionRegistry.h
    include/forge/core/FileWatcher.h
    include/forge/core/KeymapManager.h
    include/forge/core/ProjectModel.h
    include/forge/core/RpcClient.h
    include/forge/core/ServiceSupervisor.h
    include/forge/core/Settings.h
    include/forge/core/TextFileCodec.h
    include/forge/core/ProjectSearch.h
    include/forge/core/ExtensionManager.h
    include/forge/core/ThemeCatalog.h
    include/forge/core/PackageArchive.h
    include/forge/core/DiagnosticsModel.h
    src/ProjectSearch.cpp
    src/ExtensionManager.cpp
    src/ThemeCatalog.cpp
    src/PackageArchive.cpp
    src/ActionManager.cpp
    src/ServiceSupervisor.cpp
    src/RpcClient.cpp
    src/Settings.cpp
    src/TextFileCodec.cpp
    src/ProjectModel.cpp
    src/ExtensionRegistry.cpp
    src/FileWatcher.cpp
    src/KeymapManager.cpp
    src/DiagnosticsModel.cpp
)

target_include_directories(forge_core
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(forge_core
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Network
        Qt6::Concurrent
        ZLIB::ZLIB
)

target_compile_features(forge_core PUBLIC cxx_std_20)
