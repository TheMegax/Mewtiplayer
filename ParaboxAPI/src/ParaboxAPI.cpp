#include "ParaboxAPI.h"
#include "hooks/ResourceHooks.h"
#include <windows.h>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <mutex>
#include <cctype>
#include <sstream>

namespace ParaboxAPI {

static LogCallback g_logCallback = nullptr;

PARABOX_API void SetLogCallback(LogCallback cb) {
    g_logCallback = cb;
}

PARABOX_API void Log(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    if (g_logCallback) {
        g_logCallback(fmt, args);
    } else {
        // Fallback: format and send to OutputDebugString
        char buf[1024];
        vsnprintf(buf, sizeof(buf), fmt, args);
        OutputDebugStringA(buf);
        OutputDebugStringA("\n");
    }
    va_end(args);
}

PARABOX_API Event<RunFrameEvent>    OnRunFrame;
PARABOX_API Event<StevenSpawnEvent> OnStevenSpawn;

// Combat events
PARABOX_API Event<TurnStartEvent>              OnTurnStart;
PARABOX_API Event<BeginTurnEvent>              OnBeginTurn;
PARABOX_API Event<FightEndEvent>               OnFightEnd;
PARABOX_API Event<AbilityTriggerEvent>         OnAbilityTrigger;
PARABOX_API Event<EnqueueActionEvent>          OnEnqueueAction;
PARABOX_API Event<FaceDirectionEvent>          OnFaceDirection;
PARABOX_API Event<SlotUpdateDynamicValueEvent> OnSlotUpdateDynamicValue;
PARABOX_API Event<ProcessCombatInputEvent>     OnProcessCombatInput;
PARABOX_API Event<PostProcessCombatInputEvent> OnPostProcessCombatInput;
PARABOX_API Event<RouteCombatInputEvent>       OnRouteCombatInput;
PARABOX_API Event<CombatMenuShowEvent>         OnCombatMenuShow;
PARABOX_API Event<CombatMenuHideEvent>         OnCombatMenuHide;
PARABOX_API Event<ButtonCanActivateEvent>      OnButtonCanActivate;
PARABOX_API Event<ButtonActivateEvent>         OnButtonActivate;
PARABOX_API Event<PlayerBrainUpdateEvent>      OnPlayerBrainUpdate;


// Save events
PARABOX_API Event<CreateStrayCatEvent>         OnCreateStrayCat;
PARABOX_API Event<GetCollarVectorEvent>        OnGetCollarVector;

// AdventureBox events
PARABOX_API Event<ButchBoxInitEvent>           OnButchBoxInit;
PARABOX_API Event<ButchBoxTryPlaceCatEvent>    OnButchBoxTryPlaceCat;
PARABOX_API Event<ButchBoxConfigCatsEvent>     OnButchBoxConfigCats;
PARABOX_API Event<ButchBoxTryRemoveCatEvent>   OnButchBoxTryRemoveCat;
PARABOX_API Event<ButchBoxDepartEvent>         OnButchBoxDepart;
PARABOX_API Event<LoadAdventureEvent>          OnLoadAdventure;

// ClassChooser events
PARABOX_API Event<CatSelectorInitEvent>        OnCatSelectorInit;
PARABOX_API Event<ClassTagBoxClickEvent>       OnClassTagBoxClick;
PARABOX_API Event<ClassChooserLockInEvent>     OnClassChooserLockIn;

// Storage events
PARABOX_API Event<SceneManagerCreateSceneEvent> OnSceneManagerCreateScene;
PARABOX_API Event<SceneAddedEvent>              OnSceneAdded;
PARABOX_API Event<SceneAddComponentEvent>       OnSceneAddComponent;
PARABOX_API Event<InventoryItemBoxClickEvent>   OnInventoryItemBoxClick;
PARABOX_API Event<InventoryItemBoxEquippedEvent> OnInventoryItemBoxEquipped;
PARABOX_API Event<InventoryScreen2CloseEvent>   OnInventoryScreen2Close;

// Map events
PARABOX_API Event<MapNodeClickEvent>           OnMapNodeClick;
PARABOX_API Event<MapScreenEnterNodeEvent>     OnMapScreenEnterNode;
PARABOX_API Event<ResumeMapEvent>              OnResumeMap;

// ActSelection events
PARABOX_API Event<ActSelectionScreenInitEvent>      OnActSelectionScreenInit;
PARABOX_API Event<ActSelectionScreenSelectActEvent> OnActSelectionScreenSelectAct;

// LevelUp events
PARABOX_API Event<LevelUpScreenInitEvent>           OnLevelUpScreenInit;
PARABOX_API Event<LevelUpScreenSelectOptionEvent>   OnLevelUpScreenSelectOption;
PARABOX_API Event<LevelUpScreenRerollEvent>         OnLevelUpScreenReroll;
PARABOX_API Event<AbilityChooserInitEvent>          OnAbilityChooserInit;
PARABOX_API Event<AbilityChooserSelectSlotEvent>    OnAbilityChooserSelectSlot;

// WorldEvent events
PARABOX_API Event<WorldEventInitEvent>        OnWorldEventInit;
PARABOX_API Event<WorldEventClickOptionEvent> OnWorldEventClickOption;
PARABOX_API Event<WorldEventClickCatEvent>    OnWorldEventClickCat;
PARABOX_API Event<WorldEventClickEnd1Event>   OnWorldEventClickEnd1;
PARABOX_API Event<WorldEventClickEnd2Event>   OnWorldEventClickEnd2;
PARABOX_API Event<WorldEventClickEndCustomEvent> OnWorldEventClickEndCustom;

// Resource events
PARABOX_API Event<FileGetEvent>               OnFileGet;

struct FileOverrideEntry {
    const uint8_t *data = nullptr;
    size_t size = 0;
};

static std::unordered_map<std::string, FileOverrideEntry> g_fileOverrides;
static std::mutex g_fileOverridesMutex;

namespace Internal {

std::string NormalizePath(const char *path) {
    if (!path) return "";
    std::string s = path;
    for (char &c : s) {
        if (c == '\\') c = '/';
        else c = (char)tolower((unsigned char)c);
    }
    while (s.rfind("./", 0) == 0) {
        s.erase(0, 2);
    }
    while (s.rfind("/", 0) == 0) {
        s.erase(0, 1);
    }
    return s;
}

bool LookupFileOverride(const std::string &normalizedPath, const void *&outData, size_t &outSize) {
    std::lock_guard<std::mutex> lock(g_fileOverridesMutex);
    auto it = g_fileOverrides.find(normalizedPath);
    if (it != g_fileOverrides.end()) {
        outData = it->second.data;
        outSize = it->second.size;
        return true;
    }
    return false;
}

} // namespace Internal

PARABOX_API void RegisterMemoryOverride(const char *virtualPath, const void *data, size_t size) {
    if (!virtualPath || !data || size == 0) return;
    std::string norm = Internal::NormalizePath(virtualPath);
    std::lock_guard<std::mutex> lock(g_fileOverridesMutex);
    g_fileOverrides[norm] = { (const uint8_t *)data, size };
}

PARABOX_API bool HasFileOverride(const char *virtualPath) {
    if (!virtualPath) return false;
    std::string norm = Internal::NormalizePath(virtualPath);
    std::lock_guard<std::mutex> lock(g_fileOverridesMutex);
    return g_fileOverrides.find(norm) != g_fileOverrides.end();
}

PARABOX_API void RemoveFileOverride(const char *virtualPath) {
    if (!virtualPath) return;
    std::string norm = Internal::NormalizePath(virtualPath);
    std::lock_guard lock(g_fileOverridesMutex);
    g_fileOverrides.erase(norm);
}

PARABOX_API void ClearFileOverrides() {
    std::lock_guard lock(g_fileOverridesMutex);
    g_fileOverrides.clear();
}

static BOOL CALLBACK EnumMewResourcesCallback(HMODULE hModule, LPCWSTR lpType, LPWSTR lpName, LONG_PTR lParam) {
    if (!IS_INTRESOURCE(lpName)) {
        int utf8Len = WideCharToMultiByte(CP_UTF8, 0, lpName, -1, nullptr, 0, nullptr, nullptr);
        if (utf8Len > 1) {
            std::string virtualPath(utf8Len - 1, '\0');
            WideCharToMultiByte(CP_UTF8, 0, lpName, -1, &virtualPath[0], utf8Len, nullptr, nullptr);

            HRSRC hRes = FindResourceW(hModule, lpName, lpType);
            if (hRes) {
                HGLOBAL hMem = LoadResource(hModule, hRes);
                if (hMem) {
                    void *pData = LockResource(hMem);
                    DWORD size = SizeofResource(hModule, hRes);
                    if (pData && size > 0) {
                        RegisterMemoryOverride(virtualPath.c_str(), pData, (size_t)size);
                        Log("[RESOURCES] Registered embedded override: %s (%u bytes)",
                                        virtualPath.c_str(), size);
                    }
                }
            }
        }
    }
    return TRUE;
}

PARABOX_API bool RegisterModResources(HMODULE hModule) {
    if (!hModule) {
        GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCSTR)&RegisterModResources,
            &hModule
        );
    }
    if (!hModule) {
        Log("[RESOURCES] RegisterModResources failed: could not determine module handle");
        return false;
    }

    // 1. Check for manifest resource (ID 1, type L"MEWRESOURCE")
    HRSRC hManifestRes = FindResourceW(hModule, MAKEINTRESOURCEW(1), L"MEWRESOURCE");
    if (hManifestRes) {
        HGLOBAL hMem = LoadResource(hModule, hManifestRes);
        DWORD size = SizeofResource(hModule, hManifestRes);
        if (hMem && size > 0) {
            const char *pManifest = (const char *)LockResource(hMem);
            if (pManifest) {
                std::string manifestStr(pManifest, size);
                std::istringstream stream(manifestStr);
                std::string line;
                int count = 0;
                while (std::getline(stream, line)) {
                    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
                        line.pop_back();
                    }
                    if (line.empty() || line[0] == '#') continue;
                    size_t space = line.find(' ');
                    if (space == std::string::npos) continue;
                    int id = 0;
                    try {
                        id = std::stoi(line.substr(0, space));
                    } catch (...) {
                        continue;
                    }
                    std::string virtualPath = line.substr(space + 1);
                    size_t start = virtualPath.find_first_not_of(" \t");
                    if (start != std::string::npos) {
                        virtualPath = virtualPath.substr(start);
                    }

                    HRSRC hRes = FindResourceW(hModule, MAKEINTRESOURCEW(id), L"MEWRESOURCE");
                    if (hRes) {
                        HGLOBAL hResMem = LoadResource(hModule, hRes);
                        DWORD resSize = SizeofResource(hModule, hRes);
                        if (hResMem && resSize > 0) {
                            void *pData = LockResource(hResMem);
                            if (pData) {
                                RegisterMemoryOverride(virtualPath.c_str(), pData, (size_t)resSize);
                                Log("[RESOURCES] Registered embedded override: %s (%u bytes, ID %d)",
                                                virtualPath.c_str(), resSize, id);
                                count++;
                            }
                        }
                    } else {
                        Log("[RESOURCES] Failed to find resource ID %d for %s", id, virtualPath.c_str());
                    }
                }
                Log("[RESOURCES] Successfully registered %d embedded resource(s) from manifest", count);
                return count > 0;
            }
        }
    }

    // 2. Fallback: enum named resources of type MEWRESOURCE
    BOOL ok = EnumResourceNamesW(hModule, L"MEWRESOURCE", EnumMewResourcesCallback, 0);
    return ok != FALSE;
}

} // namespace ParaboxAPI

// Forward declare hook initialization functions
void MiscHooks_Init(MewjectorAPI *mj, uintptr_t gameBase);
void CombatHooks_Init(MewjectorAPI *mj, uintptr_t gameBase);
void SaveHooks_Init(MewjectorAPI *mj, uintptr_t gameBase);
void AdventureBoxHooks_Init(MewjectorAPI *mj, uintptr_t gameBase);
void CatSelectorHooks_Init(MewjectorAPI *mj, uintptr_t gameBase);
void StorageHooks_Init(MewjectorAPI *mj, uintptr_t gameBase);
void MapHooks_Init(MewjectorAPI *mj, uintptr_t gameBase);
void ActSelectionHooks_Init(MewjectorAPI *mj, uintptr_t gameBase);
void LevelUpHooks_Init(MewjectorAPI *mj, uintptr_t gameBase);
void WorldEventHooks_Init(MewjectorAPI *mj, uintptr_t gameBase);
void ShopHooks_Init(MewjectorAPI *mj, uintptr_t gameBase);
void PopupHooks_Init(MewjectorAPI *mj, uintptr_t gameBase);

namespace ParaboxAPI {

PARABOX_API void InstallHooks(MewjectorAPI *mj, uintptr_t gameBase) {
    MiscHooks_Init(mj, gameBase);
    CombatHooks_Init(mj, gameBase);
    SaveHooks_Init(mj, gameBase);
    AdventureBoxHooks_Init(mj, gameBase);
    CatSelectorHooks_Init(mj, gameBase);
    StorageHooks_Init(mj, gameBase);
    MapHooks_Init(mj, gameBase);
    ActSelectionHooks_Init(mj, gameBase);
    LevelUpHooks_Init(mj, gameBase);
    WorldEventHooks_Init(mj, gameBase);
    ShopHooks_Init(mj, gameBase);
    PopupHooks_Init(mj, gameBase);
    ResourceHooks_Init(mj, gameBase);
}

} // namespace ParaboxAPI

