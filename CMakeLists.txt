cmake_minimum_required(VERSION 4.2)

# ── Cross-compilation toolchain (Linux → Windows via Clang-MSVC) ────────────
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND NOT FORCE_LINUX)
    if(NOT CMAKE_TOOLCHAIN_FILE)
        set(CMAKE_TOOLCHAIN_FILE
            "${CMAKE_CURRENT_SOURCE_DIR}/clang-msvc-toolchain.cmake"
            CACHE FILEPATH "Clang MSVC Toolchain" FORCE)
    endif()
endif()

project(MewtiplayerWorkspace)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Choose the type of build." FORCE)
endif()

# ── Global settings ──────────────────────────────────────────────────────────
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Sub-projects ─────────────────────────────────────────────────────────────
# ParaboxAPI must be added first so Mewtiplayer can link against it
add_subdirectory(ParaboxAPI)
add_subdirectory(Mewtiplayer)
