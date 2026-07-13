cmake_minimum_required(VERSION 4.2)

# ── Sources ──────────────────────────────────────────────────────────────────
set(PARABOX_SOURCES
    # Core API
    src/ParaboxAPI.cpp
    include/ParaboxAPI.h

    # Game utilities (shared, game-facing helpers)
    include/GameUtils.h
    src/GameUtils.cpp
    include/Scanner.h
    src/Scanner.cpp
    include/MewSQL.h
    src/MewSQL.cpp
    include/MewgenicsTypes.h

    # Hooks
    include/hooks/HookMacros.h
    include/hooks/MiscHooks.h         src/hooks/MiscHooks.cpp
    include/hooks/CombatHooks.h       src/hooks/CombatHooks.cpp
    include/hooks/SaveHooks.h         src/hooks/SaveHooks.cpp
    include/hooks/AdventureBoxHooks.h src/hooks/AdventureBoxHooks.cpp
    include/hooks/ClassChooserHooks.h src/hooks/ClassChooserHooks.cpp
    include/hooks/StorageHooks.h      src/hooks/StorageHooks.cpp
    include/hooks/MapHooks.h          src/hooks/MapHooks.cpp
    include/hooks/ActSelectionHooks.h src/hooks/ActSelectionHooks.cpp
    include/hooks/LevelUpHooks.h      src/hooks/LevelUpHooks.cpp
    include/hooks/WorldEventHooks.h   src/hooks/WorldEventHooks.cpp
    include/hooks/ShopHooks.h         src/hooks/ShopHooks.cpp
)

add_library(ParaboxAPI SHARED ${PARABOX_SOURCES})

# Export symbols when building this target
target_compile_definitions(ParaboxAPI PRIVATE PARABOX_EXPORTS NOMINMAX)

target_include_directories(ParaboxAPI PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/external/mewjector
)
target_include_directories(ParaboxAPI PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/external
    ${CMAKE_CURRENT_SOURCE_DIR}/external/kiero/minhook/include
    ${CMAKE_CURRENT_SOURCE_DIR}/external/kiero/minhook/src
    ${CMAKE_CURRENT_SOURCE_DIR}/external/kiero/minhook/src/hde
)

# Compile flags
target_compile_options(ParaboxAPI PRIVATE -fms-extensions)
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(ParaboxAPI PRIVATE -fasync-exceptions -fexceptions -fcxx-exceptions)
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(ParaboxAPI PRIVATE -g -gdwarf-4)
elseif(MSVC)
    target_compile_options(ParaboxAPI PRIVATE /Zi)
    target_link_options(ParaboxAPI PRIVATE /DEBUG)
endif()

if(WIN32)
    set_target_properties(ParaboxAPI PROPERTIES PREFIX "")

    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_link_options(ParaboxAPI PRIVATE "-static-libgcc" "-static-libstdc++" "-static")
    endif()

    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
        set(STEAM_MODS_DIR "/home/megax/.local/share/Steam/steamapps/common/Mewgenics/Mods")
        add_custom_command(TARGET ParaboxAPI POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${STEAM_MODS_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:ParaboxAPI> "${STEAM_MODS_DIR}/"
            COMMENT "Copying ParaboxAPI.dll to ${STEAM_MODS_DIR}..."
        )
    endif()
endif()
