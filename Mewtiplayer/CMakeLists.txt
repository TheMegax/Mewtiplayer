cmake_minimum_required(VERSION 4.2)

# ── Sources ──────────────────────────────────────────────────────────────────
set(MEWTIPLAYER_SOURCES
    # Entry point & mod logic
    src/Mewtiplayer.cpp

    # Mewtiplayer-specific modules
    include/CrashHandler.h     src/CrashHandler.cpp
    include/ImGuiHook.h        src/ImGuiHook.cpp
    include/InputGhost.h       src/InputGhost.cpp
    include/ModState.h
    include/NetworkManager.h   src/NetworkManager.cpp
    include/Overlay.h          src/Overlay.cpp
    include/SteamABICompat.h

    # Resources
    src/resources.rc

    # Event Subscribers
    include/EventSubscribers.h
    include/events/CombatSubscribers.h       src/events/CombatSubscribers.cpp
    include/events/SaveSubscribers.h         src/events/SaveSubscribers.cpp
    include/events/AdventureBoxSubscribers.h src/events/AdventureBoxSubscribers.cpp
    include/events/ClassChooserSubscribers.h src/events/ClassChooserSubscribers.cpp
    include/events/StorageSubscribers.h      src/events/StorageSubscribers.cpp
    include/events/MapSubscribers.h          src/events/MapSubscribers.cpp
    include/events/ActSelectionSubscribers.h src/events/ActSelectionSubscribers.cpp
    include/events/LevelUpSubscribers.h      src/events/LevelUpSubscribers.cpp
    include/events/WorldEventSubscribers.h  src/events/WorldEventSubscribers.cpp
    include/events/ShopSubscribers.h        src/events/ShopSubscribers.cpp

    # imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/imconfig.h
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/imgui.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/imgui.h
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/imgui_demo.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/imgui_draw.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/imgui_impl_opengl3.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/imgui_impl_opengl3.h
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/imgui_impl_win32.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/imgui_impl_win32.h
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/imgui_internal.h
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/imgui_stdlib.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/imgui_stdlib.h
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/imgui_tables.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/imgui_widgets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/imstb_rectpack.h
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/imstb_textedit.h
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/imstb_truetype.h

    # kiero / minhook
    ${CMAKE_CURRENT_SOURCE_DIR}/external/kiero/minhook/include/MinHook.h
    ${CMAKE_CURRENT_SOURCE_DIR}/external/kiero/minhook/src/hde/hde32.c
    ${CMAKE_CURRENT_SOURCE_DIR}/external/kiero/minhook/src/hde/hde32.h
    ${CMAKE_CURRENT_SOURCE_DIR}/external/kiero/minhook/src/hde/hde64.c
    ${CMAKE_CURRENT_SOURCE_DIR}/external/kiero/minhook/src/hde/hde64.h
    ${CMAKE_CURRENT_SOURCE_DIR}/external/kiero/minhook/src/hde/pstdint.h
    ${CMAKE_CURRENT_SOURCE_DIR}/external/kiero/minhook/src/hde/table32.h
    ${CMAKE_CURRENT_SOURCE_DIR}/external/kiero/minhook/src/hde/table64.h
    ${CMAKE_CURRENT_SOURCE_DIR}/external/kiero/minhook/src/buffer.c
    ${CMAKE_CURRENT_SOURCE_DIR}/external/kiero/minhook/src/buffer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/external/kiero/minhook/src/hook.c
    ${CMAKE_CURRENT_SOURCE_DIR}/external/kiero/minhook/src/trampoline.c
    ${CMAKE_CURRENT_SOURCE_DIR}/external/kiero/minhook/src/trampoline.h
    ${CMAKE_CURRENT_SOURCE_DIR}/external/kiero/kiero.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/external/kiero/kiero.h

    # mew-ui-api
    ${CMAKE_CURRENT_SOURCE_DIR}/external/mew-ui-api/mew_ui_api.c
    ${CMAKE_CURRENT_SOURCE_DIR}/external/mew-ui-api/mew_ui_api.h

    # Steam headers (header-only)
    ${CMAKE_CURRENT_SOURCE_DIR}/external/steam/steam_api.h
    ${CMAKE_CURRENT_SOURCE_DIR}/external/steam/steam_api_common.h
    ${CMAKE_CURRENT_SOURCE_DIR}/external/steam/steam_api_flat.h
    ${CMAKE_CURRENT_SOURCE_DIR}/external/steam/steam_api_internal.h
    ${CMAKE_CURRENT_SOURCE_DIR}/external/steam/steam_gameserver.h
    ${CMAKE_CURRENT_SOURCE_DIR}/external/steam/steamclientpublic.h
    ${CMAKE_CURRENT_SOURCE_DIR}/external/steam/steamencryptedappticket.h
    ${CMAKE_CURRENT_SOURCE_DIR}/external/steam/steamhttpenums.h
    ${CMAKE_CURRENT_SOURCE_DIR}/external/steam/steamnetworkingfakeip.h
    ${CMAKE_CURRENT_SOURCE_DIR}/external/steam/steamnetworkingtypes.h
    ${CMAKE_CURRENT_SOURCE_DIR}/external/steam/steamtypes.h
    ${CMAKE_CURRENT_SOURCE_DIR}/external/steam/steamuniverse.h
    ${CMAKE_CURRENT_SOURCE_DIR}/external/stb_image.h
)

add_library(Mewtiplayer SHARED ${MEWTIPLAYER_SOURCES})

# Link against ParaboxAPI (which provides the event bus and MiscHooks)
target_link_libraries(Mewtiplayer PRIVATE ParaboxAPI)

target_compile_definitions(Mewtiplayer PRIVATE NOMINMAX PARABOX_MOD_TAG="Mewtiplayer")

target_include_directories(Mewtiplayer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/external
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/external/kiero
    ${CMAKE_CURRENT_SOURCE_DIR}/external/kiero/minhook/include
    ${CMAKE_CURRENT_SOURCE_DIR}/external/kiero/minhook/src
    ${CMAKE_CURRENT_SOURCE_DIR}/external/kiero/minhook/src/hde
    ${CMAKE_CURRENT_SOURCE_DIR}/external/mewjector
    ${CMAKE_CURRENT_SOURCE_DIR}/external/mew-ui-api
    ${CMAKE_CURRENT_SOURCE_DIR}/external/steam
    # ParaboxAPI public headers (MewgenicsTypes, GameUtils, etc.)
    ${CMAKE_SOURCE_DIR}/ParaboxAPI/include
)

set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/external/mew-ui-api/mew_ui_api.c PROPERTIES LANGUAGE CXX)

# Compile flags
target_compile_options(Mewtiplayer PRIVATE -fms-extensions)
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(Mewtiplayer PRIVATE -fasync-exceptions -fexceptions -fcxx-exceptions)
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(Mewtiplayer PRIVATE -g -gdwarf-4)
elseif(MSVC)
    target_compile_options(Mewtiplayer PRIVATE /Zi)
    target_link_options(Mewtiplayer PRIVATE /DEBUG)
endif()

if(WIN32)
    set_target_properties(Mewtiplayer PROPERTIES PREFIX "")

    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_link_options(Mewtiplayer PRIVATE "-static-libgcc" "-static-libstdc++" "-static")
    endif()

    target_link_libraries(Mewtiplayer PRIVATE
        opengl32
        gdi32
        dwmapi
        user32
        ws2_32
        dbghelp
        delayimp
        "${CMAKE_CURRENT_SOURCE_DIR}/external/steam/steam_api64.lib"
    )


    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
        set(STEAM_MODS_DIR "/home/megax/.local/share/Steam/steamapps/common/Mewgenics/Mods")
        add_custom_command(TARGET Mewtiplayer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${STEAM_MODS_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:Mewtiplayer> "${STEAM_MODS_DIR}/"
            COMMENT "Copying Mewtiplayer.dll to ${STEAM_MODS_DIR}..."
        )
    endif()
endif()
