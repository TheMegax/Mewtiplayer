#include "GameUtils.h"
#include "Overlay.h"
#include "MewSQL.h"
#include <windows.h>
#include <algorithm>
#include <map>
#include <set>

#ifndef _MSC_VER
#define __try try // NOLINT(*-reserved-identifier)
#define __except(x) catch(...) // NOLINT(*-reserved-identifier)
#endif

namespace GameUtils {

static MewDirector **g_pMewDirectorPtr = nullptr;
static TurnControl **g_pTurnControlPtr = nullptr;
static DestructString_t g_DestructString = nullptr;
static MewDirector_ctor_t g_MewDirector_ctor = nullptr;
static InitializeSave_t g_InitializeSave = nullptr;

void SetMewDirectorCtorPtr(MewDirector_ctor_t ptr) { g_MewDirector_ctor = ptr; }
void SetInitializeSavePtr(InitializeSave_t ptr) { g_InitializeSave = ptr; }
MewDirector_ctor_t GetMewDirectorCtorPtr() { return g_MewDirector_ctor; }
InitializeSave_t GetInitializeSavePtr() { return g_InitializeSave; }

void SetMewDirectorSingletonPtr(MewDirector **ptr) { g_pMewDirectorPtr = ptr; }
void SetTurnControlPtr(TurnControl **ptr) { g_pTurnControlPtr = ptr; }
void SetDestructStringPtr(DestructString_t ptr) { g_DestructString = ptr; }

void InitXString(MsvcReleaseModeXString& xstr, const std::string& str) {
  memset(&xstr, 0, sizeof(xstr));
  const size_t len = str.length();
  if (len < 16) {
    memcpy(xstr.Bx.Buf, str.c_str(), len + 1);
    xstr.Myres = 15;
  } else {
    xstr.Bx.Ptr = (char*)malloc(len + 1);
    memcpy(xstr.Bx.Ptr, str.c_str(), len + 1);
    xstr.Myres = len;
  }
  xstr.Mysize = len;
}

void FreeXString(MsvcReleaseModeXString& xstr) {
  if (g_DestructString) {
    g_DestructString(&xstr);
  } else {
    if (xstr.Myres >= 16 && xstr.Bx.Ptr) {
      free(xstr.Bx.Ptr);
    }
    memset(&xstr, 0, sizeof(xstr));
  }
}

TurnControl *GetTurnControl() {
  if (g_pTurnControlPtr)
    return *g_pTurnControlPtr;
  return nullptr;
}

MewDirector *GetMewDirectorSingleton() {
  if (g_pMewDirectorPtr)
    return *g_pMewDirectorPtr;
  return nullptr;
}

static ExecSQL_t g_ExecSQL = nullptr;
// ReSharper disable once CppParameterMayBeConst
void SetExecSQLPtr(ExecSQL_t ptr) {
  g_ExecSQL = ptr;
}
ExecSQL_t GetExecSQLPtr() {
  return g_ExecSQL;
}

void ExecuteSQL(const char* query) {
  if (!g_ExecSQL) return;
  MewDirector* dir = GetMewDirectorSingleton();
  if (!dir) return;

  void* sqlSaveFile = dir->sqlSaveFile;

  MsvcReleaseModeXString queryStr = {};
  InitXString(queryStr, query);

  void* dummyFunc[8] = {}; // Dummy std::function block (64 bytes)
  g_ExecSQL(sqlSaveFile, &queryStr, dummyFunc);
}

std::string SanitizeSQLString(const std::string& input) {
  std::string output;
  for (const char c : input) {
    if (c == '\'') {
      output += "''";
    } else {
      output += c;
    }
  }
  return output;
}

void SetSaveProperty(const std::string& key, const int value) {
  const std::string safeKey = SanitizeSQLString(key);
  char query[512];
  snprintf(query, sizeof(query), "INSERT OR REPLACE INTO properties VALUES ('%s', %d);", safeKey.c_str(), value);
  ExecuteSQL(query);
}

static ContinueFile_t g_ContinueFile = nullptr;
// ReSharper disable once CppParameterMayBeConst
void SetContinueFilePtr(ContinueFile_t ptr) {
  g_ContinueFile = ptr;
}

static StartRun_t g_StartRun = nullptr;
// ReSharper disable once CppParameterMayBeConst
void SetStartRunPtr(StartRun_t ptr) {
  g_StartRun = ptr;
}

static void** g_activeScenePtr = nullptr;
void SetActiveScenePtr(void** ptr) {
  g_activeScenePtr = ptr;
}

struct FakeSaveSelection {
  [[maybe_unused]] char pad0[0x18];    // 0x00
  Entity* entity;     // 0x18
  Scene* scene;       // 0x20
  Director* director; // 0x28
  [[maybe_unused]] char pad1[0x8];     // 0x30
  MsvcReleaseModeXString* saveStrings_first; // 0x38
  MsvcReleaseModeXString* saveStrings_last;  // 0x40
  MsvcReleaseModeXString* saveStrings_end;   // 0x48
};

static FakeSaveSelection g_fakeSaveSelection = {};
static MsvcReleaseModeXString g_fakeSaveStrings[4] = {};

int g_customTeamSize = 4;
int g_customDifficulty = 0;
int g_departureMode = 0; // 0 = Shared Progress Only, 1 = All Progress Combined
bool g_useCustomCollarClasses = false;
std::vector<std::string> g_customCollarClasses;
bool g_startCustomRunPending = false;
void* g_oldDirector = nullptr;
bool g_isLoadingCustomCats = false;
int g_currentCustomCatIndex = 1;

void SetCustomCollarClasses(const std::string& input) {
  g_customCollarClasses.clear();
  std::string item;
  for (const char c : input) {
    if (c == ',') {
      const size_t first = item.find_first_not_of(" \t\r\n");
      const size_t last = item.find_last_not_of(" \t\r\n");
      if (first != std::string::npos && last != std::string::npos) {
        g_customCollarClasses.push_back(item.substr(first, (last - first + 1)));
      }
      item.clear();
    } else {
      item.push_back(c);
    }
  }
  size_t first = item.find_first_not_of(" \t\r\n");
  size_t last = item.find_last_not_of(" \t\r\n");
  if (first != std::string::npos && last != std::string::npos) {
    g_customCollarClasses.push_back(item.substr(first, (last - first + 1)));
  }
}
static MewSaveFile_Load_t g_MewSaveFile_Load = nullptr;

void SetMewSaveFileLoadPtr(MewSaveFile_Load_t ptr) { g_MewSaveFile_Load = ptr; }
MewSaveFile_Load_t GetMewSaveFileLoadPtr() { return g_MewSaveFile_Load; }

static std::string CleanCollarClassName(const std::string& str) {
  std::string cleanStr;
  for (const char c : str) {
    if (isalnum((unsigned char)c) || c == '_' || c == ' ') {
      cleanStr.push_back(c);
    }
  }
  return cleanStr;
}

bool ParseUnlocksBlob(const std::vector<uint8_t>& blob, UnlocksData& outData) {
  if (blob.size() < 4) {
    Overlay::Log("[SAVE] [ERR] ParseUnlocksBlob: blob size %zu is too small for version", blob.size());
    return false;
  }
  outData.version = *(const uint32_t*)&blob[0];
  size_t offset = 4;

  for (int i = 0; i < 7; ++i) {
    if (offset + 8 > blob.size()) {
      Overlay::Log("[SAVE] [ERR] ParseUnlocksBlob: unexpected EOF at category %d (offset %zu, blob size %zu)", i, offset, blob.size());
      return false;
    }
    const uint64_t count = *(const uint64_t*)&blob[offset];
    offset += 8;
    outData.categories[i].reserve(count);

    for (uint64_t j = 0; j < count; ++j) {
      if (offset + 8 > blob.size()) {
        Overlay::Log("[SAVE] [ERR] ParseUnlocksBlob: unexpected EOF reading string %llu length in category %d", j, i);
        return false;
      }
      const uint64_t length = *(const uint64_t*)&blob[offset];
      offset += 8;

      if (offset + length > blob.size()) {
        Overlay::Log("[SAVE] [ERR] ParseUnlocksBlob: unexpected EOF reading string %llu content (length %llu) in category %d", j, length, i);
        return false;
      }
      std::string str((const char*)&blob[offset], length);
      offset += length;
      outData.categories[i].push_back(str);
    }
  }
  return true;
}

std::vector<uint8_t> SerializeUnlocksBlob(const UnlocksData& data) {
  std::vector<uint8_t> blob;
  blob.insert(blob.end(), (const uint8_t*)&data.version, ((const uint8_t*)&data.version) + 4);

  for (const auto & categorie : data.categories) {
    uint64_t count = categorie.size();
    blob.insert(blob.end(), (const uint8_t*)&count, ((const uint8_t*)&count) + 8);
    for (const auto& str : categorie) {
      uint64_t length = str.size();
      blob.insert(blob.end(), (const uint8_t*)&length, ((const uint8_t*)&length) + 8);
      blob.insert(blob.end(), str.begin(), str.end());
    }
  }
  return blob;
}

void MergeUnlocksBlobs(const glaiel::SQLSaveFile* db, const std::vector<std::vector<uint8_t>>& clientBlobs) {
  Overlay::Log("[SAVE] Merging %zu unlocks blobs...", clientBlobs.size());
  if (clientBlobs.empty()) return;

  std::vector<UnlocksData> clientDataList;
  clientDataList.reserve(clientBlobs.size());

  uint32_t mergedVersion = 3;

  for (const auto& blob : clientBlobs) {
    UnlocksData data;
    if (ParseUnlocksBlob(blob, data)) {
      clientDataList.push_back(data);
      mergedVersion = data.version;
    } else {
      Overlay::Log("[SAVE] [WARN] Failed to parse client unlocks blob (size %zu)", blob.size());
    }
  }

  if (clientDataList.empty()) {
    Overlay::Log("[SAVE] No valid unlocks blobs to merge.");
    return;
  }

  UnlocksData mergedData;
  mergedData.version = mergedVersion;

  for (int catIdx = 0; catIdx < 7; ++catIdx) {
    if (g_departureMode == 0) { // Shared Progress Only (Intersection)
      std::vector<std::set<std::string>> otherClientSets;
      otherClientSets.reserve(clientDataList.size() - 1);
      for (size_t c = 1; c < clientDataList.size(); ++c) {
        otherClientSets.emplace_back(clientDataList[c].categories[catIdx].begin(), clientDataList[c].categories[catIdx].end());
      }

      std::set<std::string> added;
      for (const auto& item : clientDataList[0].categories[catIdx]) {
        if (added.find(item) != added.end()) continue;
        bool inAll = true;
        for (const auto& otherSet : otherClientSets) {
          if (otherSet.find(item) == otherSet.end()) {
            inAll = false;
            break;
          }
        }
        if (inAll) {
          added.insert(item);
          mergedData.categories[catIdx].push_back(item);
        }
      }
    } else { // All Progress Combined (Union)
      std::set<std::string> added;
      for (const auto&[version, categories] : clientDataList) {
        for (const auto& item : categories[catIdx]) {
          if (added.find(item) == added.end()) {
            added.insert(item);
            mergedData.categories[catIdx].push_back(item);
          }
        }
      }
    }
    Overlay::Log("[SAVE] Category %d merged: %zu items", catIdx, mergedData.categories[catIdx].size());
  }

  std::vector<std::string> customCollars;
  for (const auto& collar : mergedData.categories[0]) {
    std::string clean = CleanCollarClassName(collar);
    if (!clean.empty() && clean.find("Colorless") == std::string::npos && clean.find("Ethereal") == std::string::npos) {
      customCollars.push_back(clean);
    }
  }

  g_customCollarClasses = customCollars;
  g_useCustomCollarClasses = true;
  Overlay::Log("[SAVE] Merged collars into %zu custom classes for broadcast/hook.", g_customCollarClasses.size());

  const std::vector<uint8_t> outBlob = SerializeUnlocksBlob(mergedData);

  Overlay::Log("[SAVE] Writing merged unlocks blob (size %zu) to database...", outBlob.size());
  std::string hexStr = "X'";
  for (const uint8_t b : outBlob) {
    char buf[3];
    snprintf(buf, sizeof(buf), "%02x", b);
    hexStr += buf;
  }
  hexStr += "'";

  const std::string query = "INSERT OR REPLACE INTO files VALUES ('unlocks', " + hexStr + ");";
  MewSQL::ExecSQLRaw(db, query);
  Overlay::Log("[SAVE] Successfully merged and wrote unlocks blob!");
}

void MergeMapFlags(glaiel::SQLSaveFile* db, const std::vector<std::vector<std::string>>& clientFlagsList) {
  Overlay::Log("[SAVE] Merging %zu map flags lists...", clientFlagsList.size());
  if (clientFlagsList.empty()) return;
  std::map<std::string, int> counts;
  for (const auto& flags : clientFlagsList) {
    for (const auto& flag : flags) {
      counts[flag]++;
    }
  }
  int mergedCount = 0;
  for (const auto&[fst, snd] : counts) {
    bool keep = false;
    if (g_departureMode == 0) { // Shared Progress Only
      if (snd == (int)clientFlagsList.size()) {
        keep = true;
      }
    } else { // All Progress Combined
      keep = true;
    }
    if (keep) {
      std::string query = "INSERT OR REPLACE INTO properties VALUES ('" + fst + "', 1);";
      MewSQL::ExecSQLOnDatabase(db, query);
      mergedCount++;
    }
  }
  Overlay::Log("[SAVE] Merged %d map flags into database.", mergedCount);
}

struct Equipment {
    uint32_t version = 5;
    bool has_equipment = false;
    std::string name;
    std::string aux_string;
    int32_t uses_left = 0;
    int32_t unknown_2 = 0;
    int32_t unknown_3 = 0;
    int32_t unknown_4 = 0;
    uint8_t unknown_5 = 0;
    uint8_t times_taken_on_adventure = 0;
};

static bool ParseEquipment(const std::vector<uint8_t>& blob, size_t& offset, Equipment& outEq) {
    if (offset + 5 > blob.size()) {
        return false;
    }
    outEq.version = *(const uint32_t*)&blob[offset];
    outEq.has_equipment = blob[offset + 4] != 0;
    offset += 5;

    if (outEq.has_equipment) {
        if (offset + 8 > blob.size()) return false;
        const uint64_t name_len = *(const uint64_t*)&blob[offset];
        offset += 8;
        if (offset + name_len > blob.size()) return false;
        outEq.name = std::string((const char*)&blob[offset], name_len);
        offset += name_len;

        if (offset + 8 > blob.size()) return false;
        const uint64_t aux_len = *(const uint64_t*)&blob[offset];
        offset += 8;
        if (offset + aux_len > blob.size()) return false;
        outEq.aux_string = std::string((const char*)&blob[offset], aux_len);
        offset += aux_len;

        if (offset + 18 > blob.size()) return false;
        outEq.uses_left = *(const int32_t*)&blob[offset];
        outEq.unknown_2 = *(const int32_t*)&blob[offset + 4];
        outEq.unknown_3 = *(const int32_t*)&blob[offset + 8];
        outEq.unknown_4 = *(const int32_t*)&blob[offset + 12];
        outEq.unknown_5 = blob[offset + 16];
        outEq.times_taken_on_adventure = blob[offset + 17];
        offset += 18;
    }
    return true;
}

static void SerializeEquipment(std::vector<uint8_t>& blob, const Equipment& eq) {
    blob.insert(blob.end(), (const uint8_t*)&eq.version, ((const uint8_t*)&eq.version) + 4);
    const uint8_t has_eq = eq.has_equipment ? 1 : 0;
    blob.push_back(has_eq);

    if (eq.has_equipment) {
        const uint64_t name_len = eq.name.size();
        blob.insert(blob.end(), (const uint8_t*)&name_len, ((const uint8_t*)&name_len) + 8);
        blob.insert(blob.end(), eq.name.begin(), eq.name.end());

        const uint64_t aux_len = eq.aux_string.size();
        blob.insert(blob.end(), (const uint8_t*)&aux_len, ((const uint8_t*)&aux_len) + 8);
        blob.insert(blob.end(), eq.aux_string.begin(), eq.aux_string.end());

        blob.insert(blob.end(), (const uint8_t*)&eq.uses_left, ((const uint8_t*)&eq.uses_left) + 4);
        blob.insert(blob.end(), (const uint8_t*)&eq.unknown_2, ((const uint8_t*)&eq.unknown_2) + 4);
        blob.insert(blob.end(), (const uint8_t*)&eq.unknown_3, ((const uint8_t*)&eq.unknown_3) + 4);
        blob.insert(blob.end(), (const uint8_t*)&eq.unknown_4, ((const uint8_t*)&eq.unknown_4) + 4);

        blob.push_back(eq.unknown_5);
        blob.push_back(eq.times_taken_on_adventure);
    }
}

void MergeInventoryBlobs(const glaiel::SQLSaveFile* db, const std::vector<std::vector<uint8_t>>& clientBlobs) {
    Overlay::Log("[SAVE] Merging %zu inventory blobs...", clientBlobs.size());
    if (clientBlobs.empty()) return;

    std::vector<std::vector<Equipment>> clientInventories;
    clientInventories.reserve(clientBlobs.size());

    for (const auto& blob : clientBlobs) {
        std::vector<Equipment> inv;
        if (blob.size() >= 4) {
            const uint32_t count = *(const uint32_t*)&blob[0];
            size_t offset = 4;
            inv.reserve(count);
            for (uint32_t i = 0; i < count; ++i) {
                Equipment eq;
                if (ParseEquipment(blob, offset, eq)) {
                    inv.push_back(eq);
                } else {
                    Overlay::Log("[SAVE] [WARN] ParseEquipment failed at item %u (offset %zu, blob size %zu)", i, offset, blob.size());
                    break;
                }
            }
        }
        clientInventories.push_back(inv);
        Overlay::Log("[SAVE] Parsed client inventory with %zu items", inv.size());
    }

    std::vector<Equipment> mergedItems;
    for (const auto& inv : clientInventories) {
        for (const auto& eq : inv) {
            if (eq.has_equipment) {
                mergedItems.push_back(eq);
            }
        }
    }

    int32_t inst_id = 1000;
    for (auto& eq : mergedItems) {
        if (eq.has_equipment) {
            eq.unknown_4 = inst_id++;
        }
    }

    Overlay::Log("[SAVE] Combined inventory has %zu items.", mergedItems.size());

    std::vector<uint8_t> outBlob;
    const uint32_t count = mergedItems.size();
    outBlob.insert(outBlob.end(), (const uint8_t*)&count, (const uint8_t*)&count + 4);

    for (const auto& eq : mergedItems) {
        SerializeEquipment(outBlob, eq);
    }

    Overlay::Log("[SAVE] Writing combined inventory blob (size %zu) to database...", outBlob.size());
    std::string hexStr = "X'";
    for (const uint8_t b : outBlob) {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02x", b);
        hexStr += buf;
    }
    hexStr += "'";

    const std::string query = "INSERT OR REPLACE INTO files VALUES ('inventory_storage', " + hexStr + ");";
    MewSQL::ExecSQLRaw(db, query);
    Overlay::Log("[SAVE] Successfully merged and wrote inventory!");
}

void CreateSaveFile(const char *saveName) {
  if (!g_MewDirector_ctor || !g_InitializeSave) {
    Overlay::Log("[SAVE] Error: CreateSaveFile called but constructor (%p) or InitializeSave (%p) not resolved!", g_MewDirector_ctor, g_InitializeSave);
    return;
  }

  Overlay::Log("[SAVE] Creating save file '%s'...", saveName);

  auto* tempDirector = (MewDirector*)malloc(sizeof(MewDirector));
  if (!tempDirector) {
    Overlay::Log("[SAVE] Error: malloc failed for temporary MewDirector!");
    return;
  }
  memset(tempDirector, 0, sizeof(MewDirector));

  g_MewDirector_ctor(tempDirector);

  MsvcReleaseModeXString saveNameXStr = {};
  InitXString(saveNameXStr, saveName);

  // Call original InitializeSave (offset +0x38 of tempDirector is GameStateMap)
  g_InitializeSave(tempDirector->gameStateMap, &saveNameXStr);

  FreeXString(saveNameXStr);
  MewSQL::CloseActiveSaveConnection(tempDirector);

  if (g_DestructString) {
    g_DestructString(&tempDirector->saveNameStr);
  }
  free(tempDirector);

  Overlay::Log("[SAVE] Save file '%s' created successfully.", saveName);
}

void CreateMewtiplayerSave(const char *saveName) {
  MewSQL::DeleteSaveFile(saveName);
  CreateSaveFile(saveName);

  if (glaiel::SQLSaveFile* db = MewSQL::OpenSaveDatabase(saveName)) {
    const std::vector<std::string> keys = {
      "mapflag_TutorialUnlocked",
      "mapflag_TutorialDone",
      "game_began",
    };

    for (const auto& key : keys) {
      char query[256];
      snprintf(query, sizeof(query), "INSERT OR IGNORE INTO properties VALUES ('%s', 1);", key.c_str());
      MewSQL::ExecSQLOnDatabase(db, query);
    }

    // Go away Tink >:(
    MewSQL::ExecSQLOnDatabase(db, "INSERT OR IGNORE INTO files VALUES "
                                  "('tutorial_tokens', "
                                  "X'02000000000000001f00000000000000636f6d626"
                                  "1745f7475746f7269616c2e676f6e2e686f7573655f"
                                  "696e74726f2200000000000000636f6d6261745f747"
                                  "5746f7269616c2e676f6e2e686f7573655f70617373"
                                  "5f646179');");

    MewSQL::CloseSaveDatabase(db);
    Overlay::Log("[SAVE] Save custom properties initialized successfully!");
  } else {
    Overlay::Log("[SAVE] Error: Failed to open offline save for initialization!");
  }
}

void LoadSaveFile(const char *saveName) {
  // This will initiate a fadeout sequence. At the end of it, it will initiate the save file with the given name,
  // creating a new mewdirector. It later takes the scene pointer from the fake save selection and destroys it,
  // loading the new save's scenes in its place.

  if (!g_ContinueFile) {
    Overlay::Log("[SAVE] ContinueFile not hooked!");
    return;
  }

  Overlay::Log("[SAVE] Triggering mod save load sequence...");
  const MewDirector* md = GetMewDirectorSingleton();
  if (!md || !md->director) {
    Overlay::Log("[SAVE] MewDirector or Director is null!");
    return;
  }

  const std::vector<Scene*> scenes = GetCurrentScenes();
  int baseIndex = -1;
  int tutorialIndex = -1;

  // Instanced scenes are loaded between Base and Tutorial
  // All other scenes are globals and should *not* be deleted!
  for (int i = 0; i < (int)scenes.size(); i++) {
    if (scenes[i]) {
      std::string name(scenes[i]->name.as_native_string_view());
      if (name == "Base") baseIndex = i;
      if (name == "Tutorial") tutorialIndex = i;
    }
  }

  if (baseIndex == -1 || tutorialIndex == -1 || tutorialIndex <= baseIndex) {
    Overlay::Log("[SAVE] Error: Could not find valid Base and Tutorial scenes!");
    return;
  }
  Scene* targetScene = scenes[tutorialIndex - 1];

  // Deconstruct scenes between Base and the targetScene
  for (int i = baseIndex + 1; i < tutorialIndex - 1; i++) {
    if (scenes[i]) {
      scenes[i]->doing_scene_destruction = true;
    }
  }

  // Find ANY valid component to proxy the Entity/Director
  Component* validComp = nullptr;
  for (int i = (int)scenes.size() - 1; i >= 0; i--) {
    std::vector<Component*> comps = GetSceneComponents(scenes[i]);
    if (!comps.empty()) {
      validComp = comps[0];
      break;
    }
  }

  if (!validComp) { // WTF?!
    Overlay::Log("[SAVE] Error: Could not find any valid components to proxy!");
    return;
  }

  memset(&g_fakeSaveSelection, 0, sizeof(g_fakeSaveSelection));
  FreeXString(g_fakeSaveStrings[1]);

  g_fakeSaveSelection.entity = validComp->entity;
  g_fakeSaveSelection.scene = targetScene;
  g_fakeSaveSelection.director = validComp->director;

  g_fakeSaveSelection.saveStrings_first = g_fakeSaveStrings;
  g_fakeSaveSelection.saveStrings_last = g_fakeSaveStrings + 4;
  g_fakeSaveSelection.saveStrings_end = g_fakeSaveStrings + 4;

  InitXString(g_fakeSaveStrings[1], saveName);

  g_ContinueFile(&g_fakeSaveSelection, 1, false);
}

void StartCustomRun(const int teamSize, const int difficulty, const int collarIndex) {
  if (!g_StartRun) {
    Overlay::Log("[SAVE] StartRun not hooked!");
    return;
  }

  MewDirector* dir = GetMewDirectorSingleton();
  if (!dir) {
    Overlay::Log("[SAVE] MewDirector is null!");
    return;
  }

  House* progressState = dir->house;
  if (!progressState) {
    Overlay::Log("[SAVE] ProgressState is null!");
    return;
  }

  // Set difficulty mod
  progressState->difficultyMod1 = difficulty;
  progressState->difficultyMod2 = difficulty;
  progressState->difficultyMod3 = difficulty;

  const auto mapName = "alley.gon";

  MsvcReleaseModeXString mapStr = {};
  InitXString(mapStr, mapName);

  Overlay::Log("[SAVE] Starting custom run: TeamSize=%d, Difficulty=%d, CollarIndex=%d", teamSize, difficulty, collarIndex);

  g_isLoadingCustomCats = true;
  g_currentCustomCatIndex = 1;

  if (!g_activeScenePtr) {
    Overlay::Log("[SAVE] Warning: g_activeScenePtr is null, calling StartRun directly");
    g_StartRun(dir, &mapStr, collarIndex, teamSize, 1);
    g_isLoadingCustomCats = false;
    return;
  }

  Scene* houseScene = GetSceneByName("House");
  if (!houseScene) {
    Overlay::Log("[RUN] Warning: 'House' scene not found, calling StartRun directly");
    g_StartRun(dir, &mapStr, collarIndex, teamSize, 1);
    g_isLoadingCustomCats = false;
    return;
  }

  void* oldContext = *g_activeScenePtr;
  *g_activeScenePtr = houseScene;
  g_StartRun(dir, &mapStr, collarIndex, teamSize, 0);
  *g_activeScenePtr = oldContext;

  g_isLoadingCustomCats = false;
}

Scene *GetSceneByName(const char *name) {
  const MewDirector *p_md = GetMewDirectorSingleton();
  if (!p_md || !p_md->director)
    return nullptr;

  for (Scene *p_scene : p_md->director->scenes) {
    if (!p_scene)
      continue;
    if (p_scene->name.as_native_string_view() == name) {
      return p_scene;
    }
  }
  return nullptr;
}

std::vector<Scene *> GetCurrentScenes() {
  std::vector<Scene *> result;
  const MewDirector *p_md = GetMewDirectorSingleton();
  if (!p_md || !p_md->director)
    return result;

  for (Scene *p_scene : p_md->director->scenes) {
    if (p_scene) {
      result.push_back(p_scene);
    }
  }
  return result;
}

std::vector<Character *> GetAllEntities() {
  std::vector<Character *> result;

  const TurnControl *tc = GetTurnControl();
  if (!tc) {
    Overlay::Log("GetFighters: TurnControl is null");
    return result;
  }

  if (!tc->context) {
    Overlay::Log("GetFighters: tc->context is null");
    return result;
  }

  const CombatContext *ctx = tc->context;
  if (!ctx->entityManager) {
    Overlay::Log("GetFighters: ctx->entityManager is null");
    return result;
  }

  const CombatEntityManager *mgr = ctx->entityManager;
  if (!mgr->stateBlock) {
    Overlay::Log("GetFighters: mgr->stateBlock is null");
    return result;
  }

  const CombatStateBlock *sb = mgr->stateBlock;
  if (!sb->fighters) {
    Overlay::Log("GetFighters: sb->fighters is null");
    return result;
  }

  const FighterList *list = sb->fighters;
  if (!list->data) {
    Overlay::Log("GetFighters: list->data is null");
    return result;
  }

  if (list->count == 0) {
    Overlay::Log("GetFighters: list->count is 0");
    return result;
  }

  for (uint32_t i = 0; i < list->count; i++) {
    if (Character *c = list->data[i]) {
      result.push_back(c);
    }
  }

  return result;
}

std::vector<Character *> GetFighters() {
  const std::vector<Character *> all = GetAllEntities();
  std::vector<Character *> fighters;

  for (Character *c : all) {
    if (c->isStatic || c->isInanimate || c->characterType == 4) {
      continue;
    }

    fighters.push_back(c);
  }

  return fighters;
}

static std::string SafeGetNativeString(const MsvcReleaseModeXString& xstr) {
  if (xstr.Mysize >= 1024 || xstr.Myres < xstr.Mysize) return "";
  
  std::string result(xstr.Mysize, '\0');
  if (xstr.Myres < 16) {
    memcpy(&result[0], xstr.Bx.Buf, xstr.Mysize);
  } else {
    if ((uintptr_t)xstr.Bx.Ptr <= 0x10000 || (uintptr_t)xstr.Bx.Ptr >= 0x7FFFFFFFFFFF) return "";
    SIZE_T bytesRead = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), xstr.Bx.Ptr, &result[0], xstr.Mysize, &bytesRead) || bytesRead != xstr.Mysize) {
      return "";
    }
  }
  return result;
}

std::string GetAbilityName(Ability *ability) {
  if (!ability || (uintptr_t)ability <= 0x10000 || (uintptr_t)ability >= 0x7FFFFFFFFFFF
      || ((uintptr_t)ability & 0xF))
    return "NULL";

  auto check_definition = [](void *p) -> std::string {
    if (!p || (uintptr_t)p <= 0x10000 || (uintptr_t)p >= 0x7FFFFFFFFFFF || ((uintptr_t)p & 0xF)) return "";
    AbilityDefinition def = {};
    SIZE_T bytesRead = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), p, &def, sizeof(AbilityDefinition), &bytesRead) || bytesRead != sizeof(AbilityDefinition)) {
        return "";
    }
    std::string name = SafeGetNativeString(def.name);
    if (!name.empty() && name.length() < 128) {
      bool printable = true;
      for (const char c : name) {
        if (c < 32 || c > 126) {
          printable = false;
          break;
        }
      }
      if (printable) {
        return name;
      }
    }
    return "";
  };

  std::string result = check_definition(ability->definition);
  if (!result.empty()) return result;

  // Fallback: only scan safe offsets if definition lookup fails.
  // Explicitly skip 0 (vtable) and 16 (owner pointer).
  for (int i = 8; i < 64; i += 8) {
    if (i == 16)
      continue; // Skip owner pointer (Character*)
      
    void *p = nullptr;
    SIZE_T bytesRead = 0;
    if (ReadProcessMemory(GetCurrentProcess(), (void*)((uintptr_t)ability + i), &p, sizeof(void*), &bytesRead) && bytesRead == sizeof(void*)) {
        result = check_definition(p);
        if (!result.empty()) return result;
    }
  }

  return "UNKNOWN";
}

Ability *FindCharacterAbility(const Character *actor, const std::string &targetName) {
  if (!actor)
    return nullptr;

  // Check direct ability pointers
  Ability *directAbilities[] = {actor->defaultMove, actor->basicAttack};
  for (Ability *directAbility : directAbilities) {
    if (directAbility && (uintptr_t)directAbility > 0x10000
        && !((uintptr_t)directAbility & 0xF)) {
      __try {
        if (directAbility->owner == actor &&
            GetAbilityName(directAbility) == targetName) {
          return directAbility;
        }
      } __except (EXCEPTION_EXECUTE_HANDLER) {
      }
    }
  }

  // Iterate spells
  if (actor != nullptr && actor->spells) {
    for (int i = 0; i < 5; i++) {
      __try {
        Ability *a = actor->spells[i];
        if (a && (uintptr_t)a > 0x10000 && (uintptr_t)a < 0x7FFFFFFFFFFF
            && !((uintptr_t)a & 0xF)) {
          if (a->owner == actor && GetAbilityName(a) == targetName) {
            return a;
          }
        }
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        break;
      }
    }
  }
  return nullptr;
}

std::vector<Component *> GetSceneComponents(const Scene *scene) {
  std::vector<Component *> result;
  if (!scene)
    return result;
  if (scene->doing_scene_destruction)
    return result;
  if (!scene->ComponentLists)
    return result;

  const podvector<Component *> &list = *scene->ComponentLists;
  for (uint32_t i = 0; i < list.size_; i++) {
    Component *p_component = list.data_[i];
    if (!p_component)
      continue;
    if (p_component->deleted)
      continue;
    result.push_back(p_component);
  }
  return result;
}

std::vector<Component *> GetEntityComponents(const Entity *entity) {
  std::vector<Component *> result;
  if (!entity)
    return result;

  const podvector<Component *> &comps = entity->components;
  for (uint32_t i = 0; i < comps.size_; i++) {
    Component *p_component = comps.data_[i];
    if (!p_component)
      continue;
    if (p_component->deleted)
      continue;
    result.push_back(p_component);
  }
  return result;
}

Component *FindComponentByTypeName(const Scene *scene, const char *typeName) {
  const std::vector<Component *> components = GetSceneComponents(scene);
  for (Component *p_component : components) {
    MsvcReleaseModeXString name = {};
    if (SafeGetComponentName(p_component, &name)) {
      const bool match = name.as_native_string_view() == typeName;
      FreeXString(name);
      if (match) {
        return p_component;
      }
    }
  }
  return nullptr;
}

bool SafeGetComponentName(const Component *p_component,
                          MsvcReleaseModeXString *out_name) {
  __try {
    if (!p_component || !p_component->vtable ||
        !p_component->vtable->GetObjectTypeSTR)
      return false;
    p_component->vtable->GetObjectTypeSTR(p_component, out_name);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

ButtonState GetButtonState(Component *button) {
  if (!button)
    return ButtonState_Invalid;
  __try {
    return (ButtonState)((Button *)button)->state;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return ButtonState_Invalid;
  }
}

bool GetButtonRoleName(Component *button, char *outBuf, const size_t bufSize) {
  if (!button || !outBuf || bufSize == 0)
    return false;
  __try {
    const auto *role = &((Button *)button)->roleName;
    if (role->Mysize > 0 && role->Mysize < 256) {
      const auto sv = role->as_native_string_view();
      const size_t len = sv.size() < bufSize - 1 ? sv.size() : bufSize - 1;
      memcpy(outBuf, sv.data(), len);
      outBuf[len] = '\0';
      return true;
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
  if (outBuf != nullptr) {
    outBuf[0] = '\0';
  }
  return false;
}

Component *FindButton(const Scene *scene, const char *roleName) {
  const std::vector<Component *> components = GetSceneComponents(scene);
  for (Component *c : components) {
    MsvcReleaseModeXString tn = {};
    if (!SafeGetComponentName(c, &tn))
      continue;
    const bool isButton = tn.as_native_string_view() == "Button";
    FreeXString(tn);
    if (!isButton)
      continue;
    __try {
      const auto *role = &((Button *)c)->roleName;
      if (role->Mysize > 0 && role->Mysize < 256 &&
          role->as_native_string_view() == roleName) {
        return c;
      }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
  }
  return nullptr;
}

std::vector<Component *> FindAllButtons(const Scene *scene, const char *roleName) {
  std::vector<Component *> result;
  const std::vector<Component *> components = GetSceneComponents(scene);
  for (Component *c : components) {
    MsvcReleaseModeXString tn = {};
    if (!SafeGetComponentName(c, &tn))
      continue;
    const bool isButton = tn.as_native_string_view() == "Button";
    FreeXString(tn);
    if (!isButton)
      continue;
    __try {
      const auto *role = &((Button *)c)->roleName;
      if (role->Mysize > 0 && role->Mysize < 256 &&
          role->as_native_string_view() == roleName) {
        result.push_back(c);
      }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
  }
  return result;
}

void ButtonGroupTracker::Init(const Scene *scene, const char *role) {
  roleName = role;
  buttons = FindAllButtons(scene, role);
  lastStates.assign(buttons.size(), ButtonState_Invalid);
}

std::vector<ButtonChange> ButtonGroupTracker::Poll() {
  std::vector<ButtonChange> changed;
  for (size_t i = 0; i < buttons.size(); i++) {
    const ButtonState state = GetButtonState(buttons[i]);
    if (state != lastStates[i]) {
      changed.push_back({static_cast<int>(i), lastStates[i], state});
      lastStates[i] = state;
    }
  }
  return changed;
}

void ButtonGroupTracker::Reset() {
  roleName = nullptr;
  buttons.clear();
  lastStates.clear();
}

void *GetThreadLocalStoragePointer() {
  // On Windows x64, the Thread Environment Block (TEB) contains a pointer
  // to the Thread Local Storage (TLS) array at gs:[0x58].
  const auto tlsArray = (void **)__readgsqword(0x58);
  if (!tlsArray)
    return nullptr;
  return tlsArray[0];
}

void SetRNGState(const void *seed32) {
  auto* tls = (GameTLSLayout*)GetThreadLocalStoragePointer();
  if (!tls)
    return;

  const auto seed = (const uint32_t *)seed32;

  // Inject the 256-bit seed directly into the Thread Local RNG State!
  tls->rngState[0] = seed[0];
  tls->rngState[1] = seed[1];
  tls->rngState[2] = seed[2];
  tls->rngState[3] = seed[3];

  tls->rngState4 = *(const uint64_t *)(seed + 4);
  tls->rngState5 = *(const uint64_t *)(seed + 6);
}

void GetRNGState(void *outSeed32) {
  const auto* tls = (const GameTLSLayout*)GetThreadLocalStoragePointer();
  if (!tls)
    return;

  auto *out = (uint32_t *)outSeed32;

  out[0] = tls->rngState[0];
  out[1] = tls->rngState[1];
  out[2] = tls->rngState[2];
  out[3] = tls->rngState[3];

  *(uint64_t *)(out + 4) = tls->rngState4;
  *(uint64_t *)(out + 6) = tls->rngState5;
}

// Note: Doesn't work, will need to look into it further
uint32_t CalculateCRC32(const void *data, size_t size) {
  uint32_t crc = 0xFFFFFFFF;
  auto p = (const uint8_t *)data;
  while (size--) {
    crc ^= *p++;
    for (int i = 0; i < 8; i++)
      crc = crc >> 1 ^ -(int32_t)(crc & 1) & 0xEDB88320;
  }
  return ~crc;
}

std::vector<UIAbilitySlot *> GetUIAbilitySlots(const CombatUISlotManager *em) {
  std::vector<UIAbilitySlot *> list;
  if (!em) return list;

  if (em->primaryEntity)
    list.push_back(em->primaryEntity);
  if (em->secondaryEntity)
    list.push_back(em->secondaryEntity);

  if (em->extraArray) {
    for (uint32_t i = 0; i < em->extraCount; i++) {
      if (em->extraArray[i])
        list.push_back(em->extraArray[i]);
    }
  }

  if (em->tertiaryEntity)
    list.push_back(em->tertiaryEntity);

  return list;
}

} // namespace GameUtils
