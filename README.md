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

  docs/                          Reference documentation
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


---


# ParaboxAPI Modding Guide

ParaboxAPI sits between the game engine and your mod. It scans the game binary
for known function signatures, installs hooks via Mewjector's chainable hook
system, and translates raw engine calls into typed events that your mod can
subscribe to. Your mod never needs to touch raw memory or function pointers
directly.


## Getting Started

Your mod is a standard Windows DLL loaded by Mewjector. In `DllMain`, resolve
the Mewjector API, register your event subscribers, then call
`ParaboxAPI::InstallHooks` to activate the framework.

```cpp
#include "ParaboxAPI.h"
#include "GameUtils.h"

static MewjectorAPI mj;

static void Initialize() {
    if (!MJ_Resolve(&mj))
        return;

    uintptr_t gameBase = mj.GetGameBase();

    // Subscribe to events before installing hooks
    ParaboxAPI::OnRunFrame.Subscribe([](ParaboxAPI::RunFrameEvent& ev) {
        // Called every game frame
    });

    ParaboxAPI::OnTurnStart.Subscribe([](ParaboxAPI::TurnStartEvent& ev) {
        // Called when a combat turn begins
    });

    // Install all hooks -- this activates the event system
    ParaboxAPI::InstallHooks(&mj, gameBase);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        Initialize();
    }
    return TRUE;
}
```


## Event System

The core of ParaboxAPI is its publish-subscribe event system. When the game
calls a hooked function, ParaboxAPI constructs an event object with the
relevant parameters and publishes it to all subscribers.

### Subscribing

```cpp
ParaboxAPI::OnClassTagBoxClick.Subscribe([](ParaboxAPI::ClassTagBoxClickEvent& ev) {
    // ev.tagBox      -- pointer to the UI element
    // ev.catID       -- which cat was clicked
    // ev.clickedIndex -- collar index that was selected
});
```

### Cancelling Events

Most events inherit from `EventBase`, which provides a `Cancel()` method. If any
subscriber cancels an event, the original game function is not called. This lets
you block or redirect game behavior.

```cpp
ParaboxAPI::OnMapNodeClick.Subscribe([](ParaboxAPI::MapNodeClickEvent& ev) {
    if (shouldBlockNavigation(ev.nodeIndex)) {
        ev.Cancel();  // Prevents the game from processing this click
    }
});
```

### Modifying Return Values

Some events expose a `returnValue` field. Setting it and cancelling the event
lets you override what the game function returns.

```cpp
ParaboxAPI::OnProcessCombatInput.Subscribe([](ParaboxAPI::ProcessCombatInputEvent& ev) {
    ev.returnValue = myCustomResult;
    ev.Cancel();
});
```


## Available Events

Events are grouped by the game system they intercept.

### General

| Event | Fires when |
|---|---|
| `OnRunFrame` | Every game frame |
| `OnStevenSpawn` | The game tries to spawn Steven |

### Combat

| Event | Fires when |
|---|---|
| `OnTurnStart` | A combat turn begins |
| `OnBeginTurn` | A character's turn starts |
| `OnFightEnd` | Combat ends |
| `OnAbilityTrigger` | An ability activates |
| `OnEnqueueAction` | A turn action is queued |
| `OnFaceDirection` | A character changes facing direction |
| `OnSlotUpdateDynamicValue` | A UI slot value updates |
| `OnProcessCombatInput` | Combat input is processed |
| `OnRouteCombatInput` | Combat input is routed to a handler |

### Save System

| Event | Fires when |
|---|---|
| `OnCreateStrayCat` | A stray cat is generated |
| `OnGetCollarVector` | The game reads collar data |

### Adventure Box (Departure)

| Event | Fires when |
|---|---|
| `OnButchBoxInit` | The adventure box initializes |
| `OnButchBoxTryPlaceCat` | A cat is placed into the box |
| `OnButchBoxConfigCats` | The box configures its cat slots |
| `OnButchBoxTryRemoveCat` | A cat is removed from the box |
| `OnButchBoxDepart` | The player departs on an adventure |
| `OnLoadAdventure` | An adventure is loaded |

### Class Chooser

| Event | Fires when |
|---|---|
| `OnCatSelectorInit` | The class chooser screen initializes |
| `OnClassTagBoxClick` | A collar/class tag is clicked |
| `OnClassChooserLockIn` | The player locks in their class selection |

### Storage and Inventory

| Event | Fires when |
|---|---|
| `OnSceneManagerCreateScene` | A new scene is created |
| `OnSceneAddComponent` | A component is added to a scene |
| `OnInventoryItemBoxClick` | An inventory item is clicked |
| `OnInventoryScreen2Close` | The inventory screen closes |

### Map

| Event | Fires when |
|---|---|
| `OnMapNodeClick` | A map node is clicked |
| `OnMapScreenEnterNode` | The player enters a map node |

### Act Selection

| Event | Fires when |
|---|---|
| `OnActSelectionScreenInit` | The act selection screen initializes |
| `OnActSelectionScreenSelectAct` | An act is selected |

### Level Up

| Event | Fires when |
|---|---|
| `OnLevelUpScreenInit` | The level up screen initializes |
| `OnLevelUpScreenSelectOption` | A level up option is selected |
| `OnLevelUpScreenReroll` | The player rerolls level up options |
| `OnAbilityChooserInit` | The ability chooser initializes |
| `OnAbilityChooserSelectSlot` | An ability slot is selected |

### World Events

| Event | Fires when |
|---|---|
| `OnWorldEventInit` | A world event screen initializes |
| `OnWorldEventClickOption` | A world event option is clicked |
| `OnWorldEventClickCat` | A cat is selected during a world event |
| `OnWorldEventClickEnd1` | The first end button is clicked |
| `OnWorldEventClickEnd2` | The second end button is clicked |


## API Functions

Beyond events, ParaboxAPI exports functions that let you manipulate game state
directly.

### Class Chooser

```cpp
ParaboxAPI::ForceClassChooserLockIn(lambdaThis);
ParaboxAPI::ApplyCollarToCharacter(cat, "CollarName");
ParaboxAPI::RefreshCatSelectorUI();
ParaboxAPI::RefreshClassChooserInventory();
ParaboxAPI::UpdateClassChooserTagBoxes(catID, collarIndex);
const char* name = ParaboxAPI::ResolveCollarNameFromIndex(index);
PersistentCharacter* cat = ParaboxAPI::GetPersistentCharacterById(catID);
```

### Storage and Inventory

```cpp
int32_t slot = ParaboxAPI::FindStorageSlotIndex(clickedBox);
ParaboxAPI::UpdateStorageItemSlot(slotIndex, catID);
int64_t catID = ParaboxAPI::ResolveSelectedCatID();
ParaboxAPI::ForceInventoryScreen2Close(self);
```

### Map

```cpp
void* screen = ParaboxAPI::GetMapScreen();
ParaboxAPI::ForceMapNodeClick(matchedNode);
```

### Act Selection

```cpp
void* screen = ParaboxAPI::GetActSelectionScreen();
ParaboxAPI::ForceActSelectionScreenSelectAct(screen, actIndex);
```

### Level Up

```cpp
auto* screen = ParaboxAPI::GetActiveLevelUpScreen();
auto* chooser = ParaboxAPI::GetActiveAbilityChooser();
ParaboxAPI::ForceLevelUpScreenSelectOption(screen, option);
ParaboxAPI::ForceLevelUpScreenReroll(screen);
ParaboxAPI::ForceAbilityChooserSelectSlot(chooser, slotIndex);
```

### World Events

```cpp
auto* event = ParaboxAPI::GetActiveWorldEvent();
ParaboxAPI::ForceWorldEventSelectOption(catUID, optionIndex);
ParaboxAPI::ForceWorldEventSelectCat(selectedCatUID);
ParaboxAPI::ForceWorldEventClickEnd(buttonType);
```

### Combat

```cpp
ParaboxAPI::ForceFaceDirection(character, packedTarget, animate, force);
```


## Game Utilities (GameUtils)

The `GameUtils` namespace provides access to game internals.

```cpp
// Singleton access
MewDirector* dir = GameUtils::GetMewDirectorSingleton();

// Scene and entity queries
auto scenes = GameUtils::GetCurrentScenes();      // ParaboxAPI::Array<Scene*>
Scene* scene = GameUtils::GetSceneByName("Fight");
auto entities = GameUtils::GetAllEntities();       // ParaboxAPI::Array<Character*>
auto fighters = GameUtils::GetFighters();

// Component inspection
auto comps = GameUtils::GetSceneComponents(scene);
Component* c = GameUtils::FindComponentByTypeName(scene, "CatSelector");

// Save file management
GameUtils::LoadSaveFile("mewtiplayer.sav");
GameUtils::CreateSaveFile("mewtiplayer.sav");

// SQL execution on the active save
GameUtils::ExecuteSQL("UPDATE properties SET value = 1 WHERE key = 'my_flag'");

// RNG manipulation
uint8_t seed[32];
GameUtils::GetRNGState(seed);
GameUtils::SetRNGState(seed);
```


## Database Access (MewSQL)

Direct access to the game's SQLite save files.

```cpp
auto* db = MewSQL::OpenSaveDatabase("saves/mewtiplayer.sav");

int64_t val = MewSQL::ReadIntFromDatabase(db, "some_key");
auto blob = MewSQL::ReadBlobFromDatabase(db, "table_name", rowKey);
auto flags = MewSQL::QueryMapFlags(db);    // ParaboxAPI::Array<SQLMapFlag>

MewSQL::ExecSQLOnDatabase(db, "INSERT INTO ...");
MewSQL::CloseSaveDatabase(db);
```


## ABI-Safe Containers

Because each DLL has its own C runtime heap, standard containers like
`std::string` and `std::vector` cannot be passed across DLL boundaries without
causing heap corruption. ParaboxAPI provides two container types that handle
this correctly.

### ParaboxAPI::String

A simple wrapper around a `char*` with an embedded free function. The producing
DLL allocates the memory, and the consuming DLL frees it through the stored
function pointer, ensuring the correct allocator is always used.

```cpp
ParaboxAPI::String str = GameUtils::GetAbilityName(ability);
printf("%s\n", str.c_str());

// Convert to std::string inside your own DLL
std::string local = str.to_string();
```

### ParaboxAPI::Array<T>

Same principle for arrays. Supports range-based for loops and conversion back
to `std::vector`.

```cpp
auto scenes = GameUtils::GetCurrentScenes();
for (Scene* s : scenes) {
    // ...
}

// Convert to std::vector inside your own DLL
std::vector<Scene*> vec = scenes.to_vector();
```

Both types are move-only. They automatically free their backing memory when
they go out of scope.


## Memory Scanning (Scanner)

For hooking functions not yet covered by ParaboxAPI, the scanner can locate
code patterns in the game binary.

```cpp
uintptr_t addr = FindPattern(gameBase, "48 89 5C 24 ?? 57 48 83 EC 20");
uintptr_t target = ResolveCall(addr + 0x15);    // Follow a CALL instruction
uintptr_t data = ResolveRIP(addr + 0x20, 3, 7); // Resolve RIP-relative address
```


## Logging

ParaboxAPI has a redirectable logging system. By default, messages go to
`OutputDebugStringA`. A mod can redirect them by providing a callback.

```cpp
ParaboxAPI::SetLogCallback(myLogFunction);
ParaboxAPI::Log("Something happened: %d", value);
```


## Linking Against ParaboxAPI

To build a mod that depends on ParaboxAPI, add it as a CMake subdirectory and
link against it:

```cmake
add_subdirectory(ParaboxAPI)
add_library(MyMod SHARED src/main.cpp)
target_link_libraries(MyMod PRIVATE ParaboxAPI)
```

This gives your mod access to all public headers (`ParaboxAPI.h`,
`GameUtils.h`, `MewSQL.h`, `MewgenicsTypes.h`, `ParaboxArray.h`, `Scanner.h`)
and the `mewjector.h` header automatically.


## License

This project is not affiliated with or endorsed by the developers of Mewgenics.
