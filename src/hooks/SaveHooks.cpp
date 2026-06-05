#include "hooks/SaveHooks.h"
#include "hooks/HookMacros.h"
#include "hooks/ModState.h"
#include "GameUtils.h"
#include "MewSQL.h"
#include "Overlay.h"
#include "Scanner.h"

HOOK_DEFINE(InitializeSave, void, void*, void*)
HOOK_DEFINE(CreateStrayCat, void*, void*)
HOOK_DEFINE(GetCollarVector, int64_t*, int64_t*, int64_t, int64_t, int64_t)

typedef void* (__fastcall *GameAllocate_t)(size_t size);
static GameAllocate_t g_GameAllocate = nullptr;

void __fastcall Hook_InitializeSave(void* gameStateMap, void* saveNameStr) {
  if (GameUtils::g_injectCustomSaveData) {
    MewSQL::CloseActiveSaveConnection(GameUtils::GetMewDirectorSingleton());
    Overlay::Log("[SAVE] Wiping save file for Start New Run...");
    MewSQL::DeleteSaveFile("mewtiplayer.sav");
  }

  // First, call the original function to create/open the DB.
  if (g_origInitializeSave) {
    g_origInitializeSave(gameStateMap, saveNameStr);
  }

  if (!GameUtils::g_injectCustomSaveData) {
      return; // Do not inject custom data into other saves
  }
  GameUtils::g_injectCustomSaveData = false;
  GameUtils::g_startCustomRunPending = true;

  Overlay::Log("[SAVE] Committing custom data...");

  std::vector<std::string> keys = {
    "mapflag_BoneyardUnlocked",
    "mapflag_BothObelisksUnlocked",
    "mapflag_BunkerUnlocked",
    "mapflag_CavesUnlocked",
    "mapflag_CoreObeliskUnlocked",
    "mapflag_CoreUnlocked",
    "mapflag_CraterUnlocked",
    "mapflag_DesertUnlocked",
    "mapflag_DimensionXUnlocked",
    "mapflag_HardPathUnlocked",
    "mapflag_JunkyardUnlocked",
    "mapflag_LabUnlocked",
    "mapflag_MeatWorldUnlocked",
    "mapflag_MeatWorldUnlockedFull",
    "mapflag_MoonObeliskUnlocked",
    "mapflag_MoonUnlocked",
    "mapflag_SewersUnlocked",
    "mapflag_ThrobbingArteryDone",
    "mapflag_WallOfFleshDone",
    "mapflag_TutorialUnlocked",
    "mapflag_TutorialDone",
    "game_began",
  };

  for (const auto& key : keys) {
    GameUtils::SetSaveProperty(key, 1);
  }

  // Go away Tink >:(
  GameUtils::ExecuteSQL("INSERT OR REPLACE INTO files VALUES "
                        "('tutorial_tokens', "
                        "X'02000000000000001f00000000000000636f6d626"
                        "1745f7475746f7269616c2e676f6e2e686f7573655f"
                        "696e74726f2200000000000000636f6d6261745f747"
                        "5746f7269616c2e676f6e2e686f7573655f70617373"
                        "5f646179');");

  Overlay::Log("[SAVE] Custom SQL executed successfully!");
}

void* __fastcall Hook_CreateStrayCat(void* catsManager) {
  void* cat = nullptr;
  if (g_origCreateStrayCat) {
    cat = g_origCreateStrayCat(catsManager);
  }

  if (GameUtils::g_isLoadingCustomCats && cat) {
    const int index = GameUtils::g_currentCustomCatIndex;
    Overlay::Log("[SAVE] Hooked CreateStrayCat, loading custom cat at index %d from test00.sav...", index);

    if (glaiel::SQLSaveFile* dbFile = MewSQL::OpenSaveDatabase("test00.sav")) {
      // ReSharper disable once CppLocalVariableMayBeConst
      if (GameUtils::MewSaveFile_Load_t mewSaveFileLoad = GameUtils::GetMewSaveFileLoadPtr()) {
        char dummySave[0x600] = {};
        memcpy(dummySave + 0x470, dbFile, sizeof(glaiel::SQLSaveFile));

        const auto catIdPtr = (int64_t*)((char*)cat + 3144);
        const int64_t originalID = *catIdPtr;

        mewSaveFileLoad((void*)dummySave, index, cat);
        *catIdPtr = originalID;

        Overlay::Log("[SAVE] Custom cat loaded successfully! OriginalID: %lld restored.", originalID);
      } else {
        Overlay::Log("[SAVE] Error: MewSaveFile::Load pointer not set!");
      }
      MewSQL::CloseSaveDatabase(dbFile);
    } else {
      Overlay::Log("[SAVE] Error: Could not open test00.sav database!");
    }
    GameUtils::g_currentCustomCatIndex++;
  }

  return cat;
}

int64_t* __fastcall Hook_GetCollarVector(int64_t* outVector, int64_t collarId, int64_t param_3, int64_t param_4) {
  if (!GameUtils::g_useCustomCollarClasses || GameUtils::g_customCollarClasses.empty()) {
    if (g_origGetCollarVector) {
      return g_origGetCollarVector(outVector, collarId, param_3, param_4);
    }
    return outVector;
  }

  Overlay::Log("[SAVE] Populating custom collar vector with %zu classes...", GameUtils::g_customCollarClasses.size());

  const size_t count = GameUtils::g_customCollarClasses.size();
  const size_t bytesToAllocate = count * sizeof(MsvcReleaseModeXString);

  if (!g_GameAllocate) {
    Overlay::Log("[SAVE] Error: GameAllocate pointer not resolved!");
    return outVector;
  }

  outVector[0] = 0;
  outVector[1] = 0;
  outVector[2] = 0;

  if (auto* array = (MsvcReleaseModeXString*)g_GameAllocate(bytesToAllocate)) {
    memset(array, 0, bytesToAllocate);
    for (size_t i = 0; i < count; ++i) {
      GameUtils::InitXString(array[i], GameUtils::g_customCollarClasses[i]);
    }
    outVector[0] = (int64_t)array;
    outVector[1] = (int64_t)(array + count);
    outVector[2] = (int64_t)(array + count);
  } else {
    Overlay::Log("[SAVE] Error: GameAllocate failed to allocate %zu bytes!", bytesToAllocate);
  }
  return outVector;
}

void SaveHooks_Init(MewjectorAPI *mj, uintptr_t gameBase) {
  HOOK_INSTALL(mj, gameBase, InitializeSave,
      "48 8B C4 48 89 58 08 48 89 50 10 55 56 57 41 54 41 55 41 56 41 57 48 8D 68 A8 48 81 EC 20 01 00 00", 16);

  // ExecSQL - set to BOTH MewSQL and GameUtils
  MewSQL::ExecSQL_t execSql = nullptr;
  SCAN_SET(mj, gameBase, ExecSQL,
      "48 89 5C 24 08 4C 89 44 24 18 48 89 54 24 10 55 56 57 48 8D 6C 24 F0 48 81 EC 10 01 00 00 49 8B D8",
      execSql);
  if (execSql) {
    MewSQL::SetExecSQLPtr(execSql);
    GameUtils::SetExecSQLPtr((GameUtils::ExecSQL_t)execSql);
  }

  // SQLSaveFile::open
  MewSQL::SQLSaveFile_open_t sqlOpen = nullptr;
  SCAN_SET(mj, gameBase, SQLSaveFile_open,
      "48 89 5C 24 08 48 89 74 24 18 48 89 54 24 10 57 48 83 EC 20 48 8B DA 48 8B F1 48 8D 79 08 48 3B FA 74 16 48 83 7A 18 0F",
      sqlOpen);
  if (sqlOpen) MewSQL::SetOpenPtr(sqlOpen);

  // SQLSaveFile::Retrieve
  MewSQL::Retrieve_t sqlRetrieve = nullptr;
  SCAN_SET(mj, gameBase, SQLSaveFile_Retrieve,
      "4C 89 44 24 18 48 89 4C 24 08 55 53 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 E9 48 81 EC C8 00 00 00 49 8B F8 4C 8B FA 33 C9 89 0A 48 8D 45 CF 48 89 45 67 48 8D 05 ?? ?? ?? ?? 48 89 45 CF",
      sqlRetrieve);
  if (sqlRetrieve) MewSQL::SetRetrievePtr(sqlRetrieve);

  // CloseConnection
  MewSQL::CloseConnection_t sqlClose = nullptr;
  SCAN_SET(mj, gameBase, CloseConnection,
      "48 89 7C 24 20 41 56 48 83 EC 30 44 8B F2 48 8B F9 48 85 C9",
      sqlClose);
  if (sqlClose) MewSQL::SetCloseConnectionPtr(sqlClose);

  // DestructString - set to BOTH MewSQL and GameUtils
  GameUtils::DestructString_t destructStr = nullptr;
  SCAN_SET(mj, gameBase, DestructString,
      "40 53 48 83 EC 20 48 8B 51 18 48 8B D9 48 83 FA 0F 76 2C 48 8B 09 48 FF C2 48 81 FA 00 10 00 00",
      destructStr);
  if (destructStr) {
    MewSQL::SetDestructStringPtr(destructStr);
    GameUtils::SetDestructStringPtr(destructStr);
  }

  // BaseSavePathLookup - RIP resolve
  MsvcReleaseModeXString *baseSavePath = nullptr;
  SCAN_RESOLVE(mj, gameBase, BaseSavePathLookup,
      "48 83 3D ?? ?? ?? ?? 0F 4C 0F 47 25 ?? ?? ?? ??",
      baseSavePath, 8, 4, 8);
  if (baseSavePath) MewSQL::SetBaseSavePathPtr(baseSavePath);

  // MewSaveFile::Load
  GameUtils::MewSaveFile_Load_t mewSaveFileLoad = nullptr;
  SCAN_SET(mj, gameBase, MewSaveFile_Load,
      "48 89 5C 24 10 48 89 74 24 18 48 89 7C 24 20 55 41 56 41 57 48 8D AC 24 30 FF FF FF",
      mewSaveFileLoad);
  if (mewSaveFileLoad) GameUtils::SetMewSaveFileLoadPtr(mewSaveFileLoad);

  // CreateStrayCat hook
  HOOK_INSTALL(mj, gameBase, CreateStrayCat,
      "48 89 5C 24 10 48 89 6C 24 18 48 89 74 24 20 57 48 83 EC 30 41 8B F8 48 8B E9", 15);

  // GameAllocate - direct function pointer
  SCAN_SET(mj, gameBase, GameAllocate,
      "48 83 EC 28 48 85 C9 75 07 33 C0 48 83 C4 28 C3 48 81 F9 00 10 00 00",
      g_GameAllocate);

  // GetCollarVector hook
  HOOK_INSTALL(mj, gameBase, GetCollarVector,
      "48 8B C4 48 89 58 10 48 89 48 08 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 F8 FD FF FF", 16);
}
