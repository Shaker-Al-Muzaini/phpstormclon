add_library(forge_ui STATIC)

target_sources(forge_ui PRIVATE
    # Editor widget comes in M2
    # For M0 this is an empty placeholder library
)

target_include_directories(forge_ui
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(forge_ui
    PUBLIC
        Qt6::Core
        Qt6::Widgets
        forge_core
)

target_compile_features(forge_ui PUBLIC cxx_std_20)
