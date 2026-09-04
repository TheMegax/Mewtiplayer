# Mewtiplayer

Mewtiplayer is an unofficial multiplayer mod for Mewgenics built on top of
ParaboxAPI, a modding framework that exposes the game's internal systems through
a structured event-driven API.

This repository contains two projects:

- **ParaboxAPI** -- A reusable modding framework that hooks into the game engine
  and publishes events that other mods can subscribe to.
- **Mewtiplayer** -- A multiplayer mod that uses ParaboxAPI to synchronize game
  state between players over Steam networking.


## Project Structure

```
Mewtiplayer/
  CMakeLists.txt                 Root build file
  clang-msvc-toolchain.cmake     Cross-compilation toolchain (Linux to Windows)
  build.sh / build.bat           Build scripts

  ParaboxAPI/                    Modding framework (outputs ParaboxAPI.dll)
    include/
      ParaboxAPI.h               Event system, event types, exported API functions
      ParaboxArray.h             ABI-safe container types for cross-DLL data
      MewgenicsTypes.h           Reconstructed game engine structures
      GameUtils.h                Game state utilities (scenes, entities, saves)
      MewSQL.h                   Save file database access
      Scanner.h                  Memory pattern scanning
      hooks/                     Hook module headers
    src/                         Hook and API implementations
    external/                    mewjector, kiero, minhook

  Mewtiplayer/                   Multiplayer mod (outputs Mewtiplayer.dll)
    include/                     Mod-specific headers
    src/                         Mod logic, event subscribers, networking
    resources/                   Cursor sprites and other embedded assets
    external/                    imgui, kiero, minhook, steam, mew-ui-api
```


## Requirements

- Mewgenics (Steam version)
- [Mewjector](https://github.com/TheMegax/Mewjector) v3+ installed as the
  game's `version.dll` proxy loader
- CMake 4.2+
- Clang with MSVC target support (for cross-compilation from Linux), or MSVC on
  Windows


## Building

On Linux (cross-compiling to Windows):

```bash
cmake -B build
cmake --build build --config Release -j$(nproc)
```

On Windows with MSVC:

```bat
cmake -B build
cmake --build build --config Release
```

The build produces `ParaboxAPI.dll` and `Mewtiplayer.dll`, and copies them into
the game's `Mods/` directory automatically if building from the expected Steam
path.


## Installation

Place both `ParaboxAPI.dll` and your mod DLL into the game's `Mods/` folder:

```
Mewgenics/
  Mods/
    ParaboxAPI.dll
    Mewtiplayer.dll        (or your own mod)
```

Mewjector loads DLLs from this directory on startup. ParaboxAPI must load before
any mod that depends on it. Use Mewjector's `LoadOrder` configuration to control
priority.


## License

This project is not affiliated with or endorsed by the developers of Mewgenics.
