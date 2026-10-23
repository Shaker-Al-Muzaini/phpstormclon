cmake_minimum_required(VERSION 3.22 FATAL_ERROR)

# ============================================================
#  Forge IDE — Top-level CMake
#  Product codename: Forge IDE
#  FORGE_PRODUCT_NAME is the single source of truth for the
#  display name; rename it here and nowhere else.
# ============================================================
set(FORGE_PRODUCT_NAME "Forge IDE" CACHE STRING "Display name of the product")
set(FORGE_VERSION_MAJOR 0)
set(FORGE_VERSION_MINOR 1)
set(FORGE_VERSION_PATCH 0)
set(FORGE_VERSION "${FORGE_VERSION_MAJOR}.${FORGE_VERSION_MINOR}.${FORGE_VERSION_PATCH}")

# vcpkg must be selected before project() so CMake initializes its toolchain.
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE
        "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "vcpkg toolchain")
    message(STATUS "Using vcpkg toolchain: ${CMAKE_TOOLCHAIN_FILE}")
endif()

project(ForgeIDE
    VERSION     ${FORGE_VERSION}
    DESCRIPTION "Native PHP/Laravel IDE"
    LANGUAGES   CXX C
)

# ============================================================
#  Standards
# ============================================================
set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

# ============================================================
#  Build type default
# ============================================================
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Setting build type to 'Debug' as none was specified.")
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose Debug or Release" FORCE)
endif()

# ============================================================
#  Output directories (put DLLs / EXEs alongside each other)
# ============================================================
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# ============================================================
#  Corrosion — integrates Cargo workspace into CMake
#  Version: v0.6.1 (latest stable as of 2026-10-21)
# ============================================================
if(NOT DEFINED Rust_TOOLCHAIN)
    set(Rust_TOOLCHAIN "1.99.1" CACHE STRING "Pinned Rust toolchain")
endif()

include(FetchContent)
FetchContent_Declare(
    Corrosion
    GIT_REPOSITORY https://github.com/corrosion-rs/corrosion.git
    GIT_TAG        v0.6.1
)
FetchContent_MakeAvailable(Corrosion)

# ============================================================
#  Qt 6.8 LTS
#  Set QT_ROOT or QT_DIR to your Qt installation, e.g.:
#    -DCMAKE_PREFIX_PATH="C:/Qt/6.8.x/msvc2022_64"
# ============================================================
find_package(Qt6 6.8 REQUIRED COMPONENTS
    Core Widgets Gui Network
)
qt_standard_project_setup()

message(STATUS "Qt version: ${Qt6_VERSION}")

# ============================================================
#  Compiler warnings (MSVC + Clang-CL)
# ============================================================
if(MSVC)
    add_compile_options(/W4 /WX /permissive- /utf-8 /MP)
    add_compile_options(/wd4068)   # suppress unknown pragma warnings (e.g. from tree-sitter headers)
    add_compile_definitions(_UNICODE UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# ============================================================
#  Global compile definitions
# ============================================================
add_compile_definitions(
    FORGE_PRODUCT_NAME="${FORGE_PRODUCT_NAME}"
    FORGE_VERSION="${FORGE_VERSION}"
    FORGE_VERSION_MAJOR=${FORGE_VERSION_MAJOR}
    FORGE_VERSION_MINOR=${FORGE_VERSION_MINOR}
    FORGE_VERSION_PATCH=${FORGE_VERSION_PATCH}
)

# ============================================================
#  Enable testing
# ============================================================
enable_testing()
include(GoogleTest)

# ============================================================
#  Rust services — import from workspace Cargo.toml
# ============================================================
corrosion_import_crate(
    MANIFEST_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml"
    CRATES phpintel vcs dbgp dbtools term ai-gateway rpc-common
)

# ============================================================
#  Sub-directories
# ============================================================
add_subdirectory(core)
add_subdirectory(ui)
add_subdirectory(app)
add_subdirectory(platform)

# ============================================================
#  Tests
# ============================================================
add_subdirectory(tests)

# ============================================================
#  Installation / packaging (stub — expanded in M8)
# ============================================================
install(TARGETS forge_app
    RUNTIME DESTINATION bin
)
