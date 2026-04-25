@echo off
setlocal

set "MODS_DIR=C:\Program Files (x86)\Steam\steamapps\common\Mewgenics\Mods"
set "CLEANUP=1"

:: If cl.exe is not in PATH, we try to call the VS Developer Command Prompt
where cl.exe >nul 2>nul
if %errorlevel% neq 0 (
    echo cl.exe not found in PATH. Attempting to set up Visual Studio environment...
    :: Common paths for vcvars64.bat
    if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
        call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
    ) else (
        echo Could not find vcvars64.bat. Please run this script from a x64 Native Tools Command Prompt.
        exit /b 1
    )
)

if not exist build mkdir build

echo Compiling resources...
rc /fo build\resources.res src\resources.rc

echo Compiling Mewtiplayer...
cl /LD /O2 /GS- /W3 /D_CRT_SECURE_NO_WARNINGS ^
  /I . /I include /I external/mewjector /I external/steam /I external/imgui /I external/kiero /I external/kiero/minhook/include ^
  Mewtiplayer.cpp src/Scanner.cpp src/NetworkManager.cpp src/Overlay.cpp src/ImGuiHook.cpp src/GameUtils.cpp src/InputGhost.cpp ^
  external/kiero/kiero.cpp ^
  external/kiero/minhook/src/buffer.c ^
  external/kiero/minhook/src/hook.c ^
  external/kiero/minhook/src/trampoline.c ^
  external/kiero/minhook/src/hde/hde64.c ^
  external/imgui/imgui.cpp ^
  external/imgui/imgui_draw.cpp ^
  external/imgui/imgui_tables.cpp ^
  external/imgui/imgui_widgets.cpp ^
  external/imgui/imgui_impl_opengl2.cpp ^
  external/imgui/imgui_impl_win32.cpp ^
  build\resources.res ^
  external/steam/steam_api64.lib user32.lib opengl32.lib gdi32.lib dwmapi.lib ^
  /Fo:build\ /Fe:build\Mewtiplayer.dll
if %errorlevel% neq 0 (
    echo Compilation failed!
    exit /b %errorlevel%
)
echo Compilation successful.

mkdir "%MODS_DIR%" 2>nul


echo Copying Mewtiplayer.dll to %MODS_DIR%...
copy /Y build\Mewtiplayer.dll "%MODS_DIR%\"

if %errorlevel% neq 0 (
    echo Failed to copy DLL. Make sure the game is closed before building.
    exit /b %errorlevel%
)

if "%CLEANUP%"=="1" (
    echo Cleaning up build files...
    rmdir /s /q build 2>nul
)

echo Done!
exit /b 0
