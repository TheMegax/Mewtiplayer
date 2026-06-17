cmake_minimum_required(VERSION 4.2)
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND NOT FORCE_LINUX)
    if(NOT CMAKE_TOOLCHAIN_FILE)
        set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/mingw-w64-toolchain.cmake" CACHE FILEPATH "MinGW Toolchain" FORCE)
    endif()
endif()

project(Mewtiplayer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(external)
include_directories(external/imgui)
include_directories(external/kiero)
include_directories(external/kiero/minhook/include)
include_directories(external/kiero/minhook/src)
include_directories(external/kiero/minhook/src/hde)
include_directories(external/mewjector)
include_directories(external/mew-ui-api)
include_directories(external/steam)
include_directories(include)

add_library(Mewtiplayer SHARED
        external/imgui/imconfig.h
        external/imgui/imgui.cpp
        external/imgui/imgui.h
        external/imgui/imgui_demo.cpp
        external/imgui/imgui_draw.cpp
        external/imgui/imgui_impl_opengl3.cpp
        external/imgui/imgui_impl_opengl3.h
        external/imgui/imgui_impl_win32.cpp
        external/imgui/imgui_impl_win32.h
        external/imgui/imgui_internal.h
        external/imgui/imgui_stdlib.cpp
        external/imgui/imgui_stdlib.h
        external/imgui/imgui_tables.cpp
        external/imgui/imgui_widgets.cpp
        external/imgui/imstb_rectpack.h
        external/imgui/imstb_textedit.h
        external/imgui/imstb_truetype.h
        external/kiero/minhook/include/MinHook.h
        external/kiero/minhook/src/hde/hde32.c
        external/kiero/minhook/src/hde/hde32.h
        external/kiero/minhook/src/hde/hde64.c
        external/kiero/minhook/src/hde/hde64.h
        external/kiero/minhook/src/hde/pstdint.h
        external/kiero/minhook/src/hde/table32.h
        external/kiero/minhook/src/hde/table64.h
        external/kiero/minhook/src/buffer.c
        external/kiero/minhook/src/buffer.h
        external/kiero/minhook/src/hook.c
        external/kiero/minhook/src/trampoline.c
        external/kiero/minhook/src/trampoline.h
        external/kiero/kiero.cpp
        external/kiero/kiero.h
        external/mew-ui-api/mew_ui_api.c
        external/mew-ui-api/mew_ui_api.h
        external/mewjector/mewjector.h
        external/steam/isteamapps.h
        external/steam/isteamappticket.h
        external/steam/isteamclient.h
        external/steam/isteamcontroller.h
        external/steam/isteamdualsense.h
        external/steam/isteamfriends.h
        external/steam/isteamgamecoordinator.h
        external/steam/isteamgameserver.h
        external/steam/isteamgameserverstats.h
        external/steam/isteamhtmlsurface.h
        external/steam/isteamhttp.h
        external/steam/isteaminput.h
        external/steam/isteaminventory.h
        external/steam/isteammatchmaking.h
        external/steam/isteammusic.h
        external/steam/isteamnetworking.h
        external/steam/isteamnetworkingmessages.h
        external/steam/isteamnetworkingsockets.h
        external/steam/isteamnetworkingutils.h
        external/steam/isteamparentalsettings.h
        external/steam/isteamps3overlayrenderer.h
        external/steam/isteamremoteplay.h
        external/steam/isteamremotestorage.h
        external/steam/isteamscreenshots.h
        external/steam/isteamtimeline.h
        external/steam/isteamugc.h
        external/steam/isteamuser.h
        external/steam/isteamuserstats.h
        external/steam/isteamutils.h
        external/steam/isteamvideo.h
        external/steam/matchmakingtypes.h
        external/steam/steam_api.h
        external/steam/steam_api_common.h
        external/steam/steam_api_flat.h
        external/steam/steam_api_internal.h
        external/steam/steam_gameserver.h
        external/steam/steamclientpublic.h
        external/steam/steamencryptedappticket.h
        external/steam/steamhttpenums.h
        external/steam/steamnetworkingfakeip.h
        external/steam/steamnetworkingtypes.h
        external/steam/steamps3params.h
        external/steam/steamtypes.h
        external/steam/steamuniverse.h
        external/stb_image.h
        include/CrashHandler.h
        include/GameUtils.h
        include/ImGuiHook.h
        include/InputGhost.h
        include/MewgenicsTypes.h
        include/NetworkManager.h
        include/Overlay.h
        include/Scanner.h
        include/MewSQL.h
        src/CrashHandler.cpp
        src/GameUtils.cpp
        src/ImGuiHook.cpp
        src/InputGhost.cpp
        src/NetworkManager.cpp
        src/Overlay.cpp
        src/resources.rc
        src/Scanner.cpp
        src/MewSQL.cpp
        include/hooks/HookMacros.h
        include/ModState.h
        include/hooks/CombatHooks.h
        include/hooks/SaveHooks.h
        include/hooks/AdventureBoxHooks.h
        include/hooks/ClassChooserHooks.h
        include/hooks/StorageHooks.h
        include/hooks/MiscHooks.h
        include/hooks/MapHooks.h
        include/hooks/ActSelectionHooks.h
        include/hooks/LevelUpHooks.h
        src/hooks/CombatHooks.cpp
        src/hooks/SaveHooks.cpp
        src/hooks/AdventureBoxHooks.cpp
        src/hooks/ClassChooserHooks.cpp
        src/hooks/StorageHooks.cpp
        src/hooks/MiscHooks.cpp
        src/hooks/MapHooks.cpp
        src/hooks/ActSelectionHooks.cpp
        src/hooks/LevelUpHooks.cpp
        Mewtiplayer.cpp)

target_compile_options(Mewtiplayer PRIVATE -fms-extensions)

set_source_files_properties(external/mew-ui-api/mew_ui_api.c PROPERTIES LANGUAGE CXX)

# Ensure debug symbols are always generated and compatible with Wine's parser
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(Mewtiplayer PRIVATE -g -gdwarf-4)
elseif(MSVC)
    target_compile_options(Mewtiplayer PRIVATE /Zi)
    target_link_options(Mewtiplayer PRIVATE /DEBUG)
endif()

if(WIN32)
    set_target_properties(Mewtiplayer PROPERTIES PREFIX "")
    
    # Static link standard libraries only when compiling with GNU (MinGW)
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
