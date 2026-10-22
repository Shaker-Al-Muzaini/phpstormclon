add_library(forge_platform STATIC
    src/Platform.cpp
)

target_include_directories(forge_platform
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(forge_platform
    PUBLIC
        Qt6::Core
        Qt6::Gui
)

target_compile_features(forge_platform PUBLIC cxx_std_20)
