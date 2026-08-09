#include "GameUtils.h"
#include "ParaboxAPI.h"
#include "MewSQL.h"
#include <windows.h>
#include <algorithm>
#include <map>
#include <set>

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

void InitXString(MsvcReleaseModeXString& xstr, const char* str_c) {
  std::string str = str_c ? str_c : "";
  memset(&xstr, 0, sizeof(xstr));
  const size_t len = str.length();
  if (len < 16) {
    memcpy(xstr.Bx.Buf, str.c_str(), len + 1);
    xstr.Myres = 15;
  } else {
    void* ptr = ParaboxAPI::GameAllocate(len + 1);
    if (!ptr) ptr = malloc(len + 1);
    xstr.Bx.Ptr = (char*)ptr;
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

void InitWString(MsvcReleaseModeWString& wstr, const wchar_t* str_c) {
  std::wstring str = str_c ? str_c : L"";
  memset(&wstr, 0, sizeof(wstr));
  const size_t len = str.length();
  if (len < 8) {
    memcpy(wstr.Bx.Buf, str.c_str(), (len + 1) * sizeof(wchar_t));
    wstr.Myres = 7;
  } else {
    void* ptr = ParaboxAPI::GameAllocate((len + 1) * sizeof(wchar_t));
    if (!ptr) ptr = malloc((len + 1) * sizeof(wchar_t));
    wstr.Bx.Ptr = (wchar_t*)ptr;
    memcpy(wstr.Bx.Ptr, str.c_str(), (len + 1) * sizeof(wchar_t));
    wstr.Myres = len;
  }
  wstr.Mysize = len;
}

void FreeWString(MsvcReleaseModeWString& wstr) {
  if (g_DestructString) {
    g_DestructString((MsvcReleaseModeXString*)&wstr);
  } else {
    if (wstr.Myres >= 8 && wstr.Bx.Ptr) {
      free(wstr.Bx.Ptr);
    }
    memset(&wstr, 0, sizeof(wstr));
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

  void* sqlSaveFile = &dir->sqlSaveFile;

  MsvcReleaseModeXString queryStr = {};
  InitXString(queryStr, query);

  void* dummyFunc[8] = {}; // Dummy std::function block (64 bytes)
  g_ExecSQL(sqlSaveFile, &queryStr, dummyFunc);
}

ParaboxAPI::String SanitizeSQLString(const char* input) {
  std::string output;
  if (input) {
    for (const char c : std::string(input)) {
      if (c == '\'') output += "''";
      else output += c;
    }
  }
  return ParaboxAPI::MakeString(output);
}

void SetSaveProperty(const char* key, const int value) {
  if (!key) return;
  const ParaboxAPI::String safeKey = SanitizeSQLString(key);
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
ParaboxAPI::Array<ParaboxAPI::String> g_customCollarClasses;
bool g_startCustomRunPending = false;
void* g_oldDirector = nullptr;
bool g_isLoadingCustomCats = false;
int g_currentCustomCatIndex = 1;

void SetCustomCollarClasses(const char* input_c) {
  if (!input_c) return;
  std::string input(input_c);
  std::vector<ParaboxAPI::String> temp;
  std::string item;
  auto add_item = [&]() {
    size_t first = item.find_first_not_of(" \t\r\n");
    size_t last = item.find_last_not_of(" \t\r\n");
    if (first != std::string::npos && last != std::string::npos) {
      temp.push_back(ParaboxAPI::MakeString(item.substr(first, (last - first + 1))));
    }
  };
  for (const char c : input) {
    if (c == ',') {
      add_item();
      item.clear();
    } else {
      item.push_back(c);
    }
  }
  add_item();
  g_customCollarClasses = ParaboxAPI::MakeArray(temp);
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

bool ParseUnlocksBlob(const ParaboxAPI::Array<uint8_t>& blob, UnlocksData& outData) {
  if (blob.size() < 4) {
    ParaboxAPI::Log("[SAVE] [ERR] ParseUnlocksBlob: blob size %zu is too small for version", blob.size());
    return false;
  }
  outData.version = *(const uint32_t*)&blob[0];
  size_t offset = 4;

  for (int i = 0; i < 7; ++i) {
    if (offset + 8 > blob.size()) {
      ParaboxAPI::Log("[SAVE] [ERR] ParseUnlocksBlob: unexpected EOF at category %d (offset %zu, blob size %zu)", i, offset, blob.size());
      return false;
    }
    const uint64_t count = *(const uint64_t*)&blob[offset];
    offset += 8;
    std::vector<ParaboxAPI::String> categoryStrings;
    categoryStrings.reserve(count);

    for (uint64_t j = 0; j < count; ++j) {
      if (offset + 8 > blob.size()) {
        ParaboxAPI::Log("[SAVE] [ERR] ParseUnlocksBlob: unexpected EOF reading string %llu length in category %d", j, i);
        return false;
      }
      const uint64_t length = *(const uint64_t*)&blob[offset];
      offset += 8;

      if (offset + length > blob.size()) {
        ParaboxAPI::Log("[SAVE] [ERR] ParseUnlocksBlob: unexpected EOF reading string %llu content (length %llu) in category %d", j, length, i);
        return false;
      }
      std::string str((const char*)&blob[offset], length);
      offset += length;
      categoryStrings.push_back(ParaboxAPI::MakeString(str));
    }
    outData.categories[i] = ParaboxAPI::MakeArray(categoryStrings);
  }
  return true;
}

ParaboxAPI::Array<uint8_t> SerializeUnlocksBlob(const UnlocksData& data) {
  std::vector<uint8_t> blob;
  blob.insert(blob.end(), (const uint8_t*)&data.version, ((const uint8_t*)&data.version) + 4);

  for (const auto & categorie : data.categories) {
    uint64_t count = categorie.size();
    blob.insert(blob.end(), (const uint8_t*)&count, ((const uint8_t*)&count) + 8);
    for (size_t i = 0; i < count; ++i) {
      const auto& str = categorie[i];
      uint64_t length = str.size();
      blob.insert(blob.end(), (const uint8_t*)&length, ((const uint8_t*)&length) + 8);
      if (length > 0) blob.insert(blob.end(), (const uint8_t*)str.c_str(), (const uint8_t*)str.c_str() + length);
    }
  }
  return ParaboxAPI::MakeArray(blob);
}

void MergeUnlocksBlobs(const glaiel::SQLSaveFile* db, const ParaboxAPI::Array<ParaboxAPI::Array<uint8_t>>& clientBlobs) {
  ParaboxAPI::Log("[SAVE] Merging %zu unlocks blobs...", clientBlobs.size());
  if (clientBlobs.empty()) return;

  std::vector<UnlocksData> clientDataList;
  clientDataList.reserve(clientBlobs.size());

  uint32_t mergedVersion = 3;

  for (const auto & clientBlob : clientBlobs) {
    UnlocksData data;
    if (ParseUnlocksBlob(clientBlob, data)) {
      clientDataList.push_back(std::move(data));
      mergedVersion = data.version;
    } else {
      ParaboxAPI::Log("[SAVE] [WARN] Failed to parse client unlocks blob (size %zu)", clientBlob.size());
    }
  }

  if (clientDataList.empty()) {
    ParaboxAPI::Log("[SAVE] No valid unlocks blobs to merge.");
    return;
  }

  UnlocksData mergedData;
  mergedData.version = mergedVersion;

  for (int catIdx = 0; catIdx < 7; ++catIdx) {
    if (g_departureMode == 0) { // Shared Progress Only (Intersection)
      std::vector<std::set<std::string>> otherClientSets;
      otherClientSets.reserve(clientDataList.size() - 1);
      for (size_t c = 1; c < clientDataList.size(); ++c) {
        std::set<std::string> s;
        for (const auto & i : clientDataList[c].categories[catIdx]) {
          s.insert(i.to_string());
        }
        otherClientSets.push_back(s);
      }

      std::set<std::string> added;
      std::vector<ParaboxAPI::String> mergedCats;
      for (const auto & i : clientDataList[0].categories[catIdx]) {
        std::string item = i.to_string();
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
          mergedCats.push_back(ParaboxAPI::MakeString(item));
        }
      }
      mergedData.categories[catIdx] = ParaboxAPI::MakeArray(mergedCats);
    } else { // All Progress Combined (Union)
      std::set<std::string> added;
      std::vector<ParaboxAPI::String> mergedCats;
      for (auto&[version, categories] : clientDataList) {
        for (const auto & i : categories[catIdx]) {
          std::string item = i.to_string();
          if (added.find(item) == added.end()) {
            added.insert(item);
            mergedCats.push_back(ParaboxAPI::MakeString(item));
          }
        }
      }
      mergedData.categories[catIdx] = ParaboxAPI::MakeArray(mergedCats);
    }
    ParaboxAPI::Log("[SAVE] Category %d merged: %zu items", catIdx, mergedData.categories[catIdx].size());
  }

  std::vector<ParaboxAPI::String> customCollars;
  for (const auto & i : mergedData.categories[0]) {
    std::string clean = CleanCollarClassName(i.to_string());
    if (!clean.empty() && clean.find("Colorless") == std::string::npos && clean.find("Ethereal") == std::string::npos) {
      customCollars.push_back(ParaboxAPI::MakeString(clean));
    }
  }

  g_customCollarClasses = ParaboxAPI::MakeArray(customCollars);
  g_useCustomCollarClasses = true;
  ParaboxAPI::Log("[SAVE] Merged collars into %zu custom classes for broadcast/hook.", g_customCollarClasses.size());

  const ParaboxAPI::Array<uint8_t> outBlob = SerializeUnlocksBlob(mergedData);

  ParaboxAPI::Log("[SAVE] Writing merged unlocks blob (size %zu) to database...", outBlob.size());
  std::string hexStr = "X'";
  for (unsigned char i : outBlob) {
    char buf[3];
    snprintf(buf, sizeof(buf), "%02x", i);
    hexStr += buf;
  }
  hexStr += '\'';

  const std::string query = "INSERT OR REPLACE INTO files VALUES ('unlocks', " + hexStr + ");";
  MewSQL::ExecSQLRaw(db, query.c_str());
  ParaboxAPI::Log("[SAVE] Successfully merged and wrote unlocks blob!");
}

void MergeMapFlags(glaiel::SQLSaveFile* db, const ParaboxAPI::Array<ParaboxAPI::Array<ParaboxAPI::String>>& clientFlagsList) {
  ParaboxAPI::Log("[SAVE] Merging %zu map flags lists...", clientFlagsList.size());
  if (clientFlagsList.empty()) return;
  std::map<std::string, int> counts;
  for (const auto & i : clientFlagsList) {
    for (const auto & j : i) {
      counts[j.to_string()]++;
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
      MewSQL::ExecSQLOnDatabase(db, query.c_str());
      mergedCount++;
    }
  }
  ParaboxAPI::Log("[SAVE] Merged %d map flags into database.", mergedCount);
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

static bool ParseEquipment(const ParaboxAPI::Array<uint8_t>& blob, size_t& offset, Equipment& outEq) {
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

void MergeInventoryBlobs(const glaiel::SQLSaveFile* db, const ParaboxAPI::Array<ParaboxAPI::Array<uint8_t>>& clientBlobs) {
    ParaboxAPI::Log("[SAVE] Merging %zu inventory blobs...", clientBlobs.size());
    if (clientBlobs.empty()) return;

    std::vector<std::vector<Equipment>> clientInventories;
    clientInventories.reserve(clientBlobs.size());

    for (const auto & blob : clientBlobs) {
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
                    ParaboxAPI::Log("[SAVE] [WARN] ParseEquipment failed at item %u (offset %zu, blob size %zu)", i, offset, blob.size());
                    break;
                }
            }
        }
        clientInventories.push_back(inv);
        ParaboxAPI::Log("[SAVE] Parsed client inventory with %zu items", inv.size());
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

    ParaboxAPI::Log("[SAVE] Combined inventory has %zu items.", mergedItems.size());

    std::vector<uint8_t> outBlob;
    const uint32_t count = mergedItems.size();
    outBlob.insert(outBlob.end(), (const uint8_t*)&count, (const uint8_t*)&count + 4);

    for (const auto& eq : mergedItems) {
        SerializeEquipment(outBlob, eq);
    }

    ParaboxAPI::Log("[SAVE] Writing combined inventory blob (size %zu) to database...", outBlob.size());
    std::string hexStr = "X'";
    for (const uint8_t b : outBlob) {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02x", b);
        hexStr += buf;
    }
    hexStr += '\'';

    const std::string query = "INSERT OR REPLACE INTO files VALUES ('inventory_storage', " + hexStr + ");";
    MewSQL::ExecSQLRaw(db, query.c_str());
    ParaboxAPI::Log("[SAVE] Successfully merged and wrote inventory!");
}

void CreateSaveFile(const char *saveName) {
  if (!g_MewDirector_ctor || !g_InitializeSave) {
    ParaboxAPI::Log("[SAVE] Error: CreateSaveFile called but constructor (%p) or InitializeSave (%p) not resolved!", g_MewDirector_ctor, g_InitializeSave);
    return;
  }

  ParaboxAPI::Log("[SAVE] Creating save file '%s'...", saveName);

  auto* tempDirector = (MewDirector*)malloc(sizeof(MewDirector));
  if (!tempDirector) {
    ParaboxAPI::Log("[SAVE] Error: malloc failed for temporary MewDirector!");
    return;
  }
  memset(tempDirector, 0, sizeof(MewDirector));

  g_MewDirector_ctor(tempDirector);

  MsvcReleaseModeXString saveNameXStr = {};
  InitXString(saveNameXStr, saveName);

  // Call original InitializeSave (offset +0x38 of tempDirector is GameStateMap)
  g_InitializeSave(tempDirector->gameStateMap, &saveNameXStr); // g_InitializeSave destructs saveNameXStr

  MewSQL::CloseActiveSaveConnection(tempDirector);

  if (g_DestructString) {
    g_DestructString(&tempDirector->sqlSaveFile.db_path_string);
  }
  free(tempDirector);

  ParaboxAPI::Log("[SAVE] Save file '%s' created successfully.", saveName);
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
                                  "X'03000000000000001f0000000000000063"
                                  "6f6d6261745f7475746f7269616c2e676f6e"
                                  "2e686f7573655f696e74726f220000000000"
                                  "0000636f6d6261745f7475746f7269616c2e"
                                  "676f6e2e686f7573655f706173735f646179"
                                  "2700000000000000636f6d6261745f747574"
                                  "6f7269616c2e676f6e2e696e74726f647563"
                                  "655f686172645f70617468');");

    MewSQL::CloseSaveDatabase(db);
    ParaboxAPI::Log("[SAVE] Save custom properties initialized successfully!");
  } else {
    ParaboxAPI::Log("[SAVE] Error: Failed to open offline save for initialization!");
  }
}

static Director_DestroyScene_t g_DestroyScene = nullptr;
void SetDestroyScenePtr(Director_DestroyScene_t ptr) { g_DestroyScene = ptr; }

void DestroyScene(const char *sceneName) {
  if (!g_DestroyScene) {
    ParaboxAPI::Log("[SCENE] DestroyScene pointer is null!");
    return;
  }
  MewDirector* md = GetMewDirectorSingleton();
  if (!md || !md->director) {
    ParaboxAPI::Log("[SCENE] MewDirector or Director is null!");
    return;
  }
  MsvcReleaseModeXString str = {};
  InitXString(str, sceneName);
  g_DestroyScene(md->director, &str);
}

void LoadSaveFile(const char *saveName) {
  // This will initiate a fadeout sequence. At the end of it, it will initiate the save file with the given name,
  // creating a new mewdirector. It later takes the scene pointer from the fake save selection and destroys it,
  // loading the new save's scenes in its place.

  if (!g_ContinueFile) {
    ParaboxAPI::Log("[SAVE] ContinueFile not hooked!");
    return;
  }

  ParaboxAPI::Log("[SAVE] Triggering mod save load sequence...");
  MewDirector* md = GetMewDirectorSingleton();
  if (!md || !md->director) {
    ParaboxAPI::Log("[SAVE] MewDirector or Director is null!");
    return;
  }

  if (md->inCombat) {
    ParaboxAPI::Log("[SAVE] MewDirector was in combat, deactivating combat state...");
    md->inCombat = false;
  }

  auto scenes = GetCurrentScenes();
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
    ParaboxAPI::Log("[SAVE] Error: Could not find valid Base and Tutorial scenes!");
    return;
  }
  Scene* targetScene = scenes[tutorialIndex - 1];

  // Deconstruct and destroy scenes between Base and the targetScene
  for (int i = baseIndex + 1; i < tutorialIndex - 1; i++) {
    if (scenes[i]) {
      std::string sceneName(scenes[i]->name.as_native_string_view());
      ParaboxAPI::Log("[SAVE] Destroying intermediate scene '%s'", sceneName.c_str());
      DestroyScene(sceneName.c_str());
      scenes[i]->doing_scene_destruction = true;
    }
  }

  // Find ANY valid component to proxy the Entity/Director
  Component* validComp = nullptr;
  for (int i = (int)scenes.size() - 1; i >= 0; i--) {
    auto comps = GetSceneComponents(scenes[i]);
    if (!comps.empty()) {
      validComp = comps[0];
      break;
    }
  }

  if (!validComp) { // WTF?!
    ParaboxAPI::Log("[SAVE] Error: Could not find any valid components to proxy!");
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
    ParaboxAPI::Log("[SAVE] StartRun not hooked!");
    return;
  }

  MewDirector* dir = GetMewDirectorSingleton();
  if (!dir) {
    ParaboxAPI::Log("[SAVE] MewDirector is null!");
    return;
  }

  House* progressState = dir->house;
  if (!progressState) {
    ParaboxAPI::Log("[SAVE] ProgressState is null!");
    return;
  }

  // Set difficulty mod
  progressState->difficultyMod1 = difficulty;
  progressState->difficultyMod2 = difficulty;
  progressState->difficultyMod3 = difficulty;

  const auto mapName = "alley.gon";

  MsvcReleaseModeXString mapStr = {};
  InitXString(mapStr, mapName);

  ParaboxAPI::Log("[SAVE] Starting custom run: TeamSize=%d, Difficulty=%d, CollarIndex=%d", teamSize, difficulty, collarIndex);

  g_isLoadingCustomCats = true;
  g_currentCustomCatIndex = 1;

  if (!g_activeScenePtr) {
    ParaboxAPI::Log("[SAVE] Warning: g_activeScenePtr is null, calling StartRun directly");
    g_StartRun(dir, &mapStr, collarIndex, teamSize, 1);
    g_isLoadingCustomCats = false;
    return;
  }

  Scene* houseScene = GetSceneByName("House");
  if (!houseScene) {
    ParaboxAPI::Log("[RUN] Warning: 'House' scene not found, calling StartRun directly");
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

ParaboxAPI::Array<Scene *> GetCurrentScenes() {
  std::vector<Scene *> result;
  const MewDirector *p_md = GetMewDirectorSingleton();
  if (!p_md || !p_md->director) return ParaboxAPI::MakeArray(result);
  for (Scene *p_scene : p_md->director->scenes) {
    if (p_scene) result.push_back(p_scene);
  }
  return ParaboxAPI::MakeArray(result);
}

ParaboxAPI::Array<Character *> GetAllEntities() {
  std::vector<Character *> result;
  __try {
    const TurnControl *tc = GetTurnControl();
    if (!tc || !tc->context || !tc->context->entityManager || !tc->context->entityManager->stateBlock || !tc->context->entityManager->stateBlock->fighters || !tc->context->entityManager->stateBlock->fighters->data) return ParaboxAPI::MakeArray(result);
    const FighterList *list = tc->context->entityManager->stateBlock->fighters;
    for (uint32_t i = 0; i < list->count; i++) {
      if (Character *c = list->data[i]) result.push_back(c);
    }
  } __except(EXCEPTION_EXECUTE_HANDLER) {
    result.clear();
  }
  return ParaboxAPI::MakeArray(result);
}

ParaboxAPI::Array<Character *> GetFighters() {
  const auto all = GetAllEntities();
  std::vector<Character *> fighters;
  for (auto c : all) {
    if (c->isStatic || c->isInanimate || c->characterType == 4) continue;
    fighters.push_back(c);
  }
  return ParaboxAPI::MakeArray(fighters);
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

static bool SafeReadProcessMemory(void* addr, void* buf, size_t len) {
  if (!addr || (uintptr_t)addr <= 0x10000 || (uintptr_t)addr >= 0x7FFFFFFFFFFF) return false;
  SIZE_T bytesRead = 0;
  bool ok = false;
  __try {
    if (ReadProcessMemory(GetCurrentProcess(), addr, buf, len, &bytesRead) && bytesRead == len) {
      ok = true;
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    ok = false;
  }
  return ok;
}

static std::string CheckAbilityDefinitionPtr(void *p) {
  if (!p || (uintptr_t)p <= 0x10000 || (uintptr_t)p >= 0x7FFFFFFFFFFF) return "";
  AbilityDefinition def = {};
  if (!SafeReadProcessMemory(p, &def, sizeof(AbilityDefinition))) return "";
  std::string name = SafeGetNativeString(def.name);
  if (!name.empty() && name.length() < 128) {
    bool printable = true;
    for (const char c : name) { if (c < 32 || c > 126) { printable = false; break; } }
    if (printable) return name;
  }
  return "";
}

ParaboxAPI::String GetAbilityName(Ability *ability) {
  if (!ability || (uintptr_t)ability <= 0x10000 || (uintptr_t)ability >= 0x7FFFFFFFFFFF) return ParaboxAPI::MakeString("NULL");

  void* defPtr = nullptr;
  if (SafeReadProcessMemory((void*)((uintptr_t)ability + offsetof(Ability, definition)), &defPtr, sizeof(void*))) {
    std::string result = CheckAbilityDefinitionPtr(defPtr);
    if (!result.empty()) return ParaboxAPI::MakeString(result);
  }

  for (int i = 8; i < 64; i += 8) {
    if (i == 16) continue;
    void *p = nullptr;
    if (SafeReadProcessMemory((void*)((uintptr_t)ability + i), &p, sizeof(void*))) {
        std::string result = CheckAbilityDefinitionPtr(p);
        if (!result.empty()) return ParaboxAPI::MakeString(result);
    }
  }
  return ParaboxAPI::MakeString("UNKNOWN");
}

static bool IsPointerReadable(const void* ptr) {
    if (!ptr || (uintptr_t)ptr <= 0x10000 || (uintptr_t)ptr >= 0x7FFFFFFFFFFF) {
        return false;
    }
    return true;
}

static bool CheckAbilityNameMatch(Ability *a, const std::string &targetName) {
    if (!IsPointerReadable(a) || ((uintptr_t)a & 0x7)) return false;
    const std::string name = GetAbilityName(a).to_string();
    return (!name.empty() && name != "UNKNOWN" && name != "NULL" && name == targetName);
}

typedef Ability* (__fastcall *FnSpawnDatabaseCreateAbility)(void* spawnDb, Character* actor, const MsvcReleaseModeXString* nameStr, Ability* parent);

static Ability* SafeInvokeCreateAbility(FnSpawnDatabaseCreateAbility fnCreate, Component* spawnDb, Character* actor, const MsvcReleaseModeXString* nameStr) {
  Ability* res = nullptr;
  __try {
    res = fnCreate(spawnDb, actor, nameStr, nullptr);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    res = nullptr;
  }
  return res;
}

static Component* GetSpawnDatabaseComponent() {
    const auto scenes = GetCurrentScenes();
    for (const auto* scene : scenes) {
        if (!scene) continue;
        if (Component* comp = FindComponentByTypeName(scene, "SpawnDatabase")) return comp;
    }
    return nullptr;
}

static Ability* CreateAbilityFromSpawnDatabase(Character* actor, const char* abilityName) {
    if (!actor || !abilityName || !*abilityName) return nullptr;

    const auto gameBase = (uintptr_t)GetModuleHandleA(nullptr);
    if (!gameBase) return nullptr;

    const auto fnCreate = (FnSpawnDatabaseCreateAbility)(void*)(gameBase + 0x7A99B0);
    Component* spawnDb = GetSpawnDatabaseComponent();
    if (!spawnDb) {
        ParaboxAPI::Log("[SPAWNDB] Could not find SpawnDatabase component on active scenes");
        return nullptr;
    }

    MsvcReleaseModeXString nameStr = {};
    InitXString(nameStr, abilityName);

    Ability* createdAbility = SafeInvokeCreateAbility(fnCreate, spawnDb, actor, &nameStr); // fnCreate destructs nameStr

    if (createdAbility) {
        std::string charName = (actor) ? actor->name.to_utf8() : "Unknown";
        if (charName.empty() || charName == "UNKNOWN" || charName == "NULL") charName = "Character";
        ParaboxAPI::Log("[SPAWNDB] Dynamically created ability '%s' for %s", abilityName, charName.c_str());
    } else {
        ParaboxAPI::Log("[SPAWNDB] CreateAbility returned null for '%s'", abilityName);
    }

    return createdAbility;
}

Ability *FindCharacterAbility(const Character *actor, const char *targetName_c) {
  const std::string targetName = targetName_c ? targetName_c : "";
  if (!actor || targetName.empty())
    return nullptr;

  // Check direct ability pointers
  if (CheckAbilityNameMatch(actor->ability0, targetName)) return actor->ability0;
  if (CheckAbilityNameMatch(actor->defaultMove, targetName)) return actor->defaultMove;
  if (CheckAbilityNameMatch(actor->basicAttack, targetName)) return actor->basicAttack;
  if (CheckAbilityNameMatch(actor->bonusAbility, targetName)) return actor->bonusAbility;

  // Iterate spells
  if (actor->spells && IsPointerReadable(actor->spells) && actor->spellCount > 0 && actor->spellCount < 100) {
    for (uint32_t i = 0; i < actor->spellCount; ++i) {
      Ability* a = actor->spells[i];
      if (CheckAbilityNameMatch(a, targetName)) {
        return a;
      }
    }
  }

  // Iterate passives / extra abilities vector
  if (actor->passives && IsPointerReadable(actor->passives) && actor->passivesCount > 0 && actor->passivesCount < 200) {
    for (uint32_t i = 0; i < actor->passivesCount; ++i) {
      Ability* a = actor->passives[i];
      if (CheckAbilityNameMatch(a, targetName)) {
        return a;
      }
    }
  }

  // Scan all active scene components for matching Ability definition name
  const auto scenes = GetCurrentScenes();
  for (const auto* scene : scenes) {
    if (!scene) continue;
    const auto comps = GetSceneComponents(scene);
    for (auto* c : comps) {
      if (!c) continue;
      auto* aComp = reinterpret_cast<Ability*>(c);
      if (CheckAbilityNameMatch(aComp, targetName)) {
        return aComp;
      }
    }
  }

  // Dynamically construct missing ability via SpawnDatabase
  return CreateAbilityFromSpawnDatabase(const_cast<Character*>(actor), targetName_c);
}

Component *FindCharacterPassive(const Character *actor, const char *targetName_c) {
  const std::string targetName = targetName_c ? targetName_c : "";
  if (targetName.empty()) return nullptr;

  // Scan scene components for matching Component ObjectTypeSTR
  const auto scenes = GetCurrentScenes();
  for (const auto* scene : scenes) {
    if (!scene) continue;
    const auto comps = GetSceneComponents(scene);
    for (auto* c : comps) {
      if (!c || !c->vtable || !c->vtable->GetObjectTypeSTR) continue;
      MsvcReleaseModeXString xstr = {};
      if (SafeGetComponentName(c, &xstr)) {
        const std::string typeName = SafeGetNativeString(xstr);
        FreeXString(xstr);
        if (typeName == targetName) {
          return c;
        }
      }
    }
  }

  // Fallback to FindCharacterAbility
  if (actor) {
    if (Ability* a = FindCharacterAbility(actor, targetName_c)) {
      return reinterpret_cast<Component*>(a);
    }
  }

  return nullptr;
}

ParaboxAPI::Array<Component *> GetSceneComponents(const Scene *scene) {
  std::vector<Component *> result;
  if (!scene || scene->doing_scene_destruction || !scene->ComponentLists) return ParaboxAPI::MakeArray(result);
  const podvector<Component *> &list = *scene->ComponentLists;
  for (uint32_t i = 0; i < list.size_; i++) {
    Component *p_component = list.data_[i];
    if (p_component && !p_component->deleted) result.push_back(p_component);
  }
  return ParaboxAPI::MakeArray(result);
}

ParaboxAPI::Array<Component *> GetEntityComponents(const Entity *entity) {
  std::vector<Component *> result;
  if (!entity) return ParaboxAPI::MakeArray(result);
  const podvector<Component *> &comps = entity->components;
  for (uint32_t i = 0; i < comps.size_; i++) {
    Component *p_component = comps.data_[i];
    if (p_component && !p_component->deleted) result.push_back(p_component);
  }
  return ParaboxAPI::MakeArray(result);
}

Component *FindComponentByTypeName(const Scene *scene, const char *typeName) {
  const auto components = GetSceneComponents(scene);
  for (const auto p_component : components) {
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

bool IsComponentValid(const void *component) {
  if (!component) return false;
  const auto *comp = static_cast<const Component *>(component);
  if (comp->deleted) return false;
  if (!comp->scene || comp->scene->doing_scene_destruction) return false;

  const auto scenes = GetCurrentScenes();
  return std::any_of(scenes.begin(), scenes.end(),
                     [comp](const Scene *s) { return s == comp->scene; });
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

ParaboxAPI::Array<UIAbilitySlot *> GetUIAbilitySlots(const CombatUISlotManager *em) {
  std::vector<UIAbilitySlot *> list;
  if (!em) return ParaboxAPI::MakeArray(list);
  if (em->primaryEntity) list.push_back(em->primaryEntity);
  if (em->secondaryEntity) list.push_back(em->secondaryEntity);
  if (em->extraArray) {
    for (uint32_t i = 0; i < em->extraCount; i++) {
      if (em->extraArray[i]) list.push_back(em->extraArray[i]);
    }
  }
  if (em->tertiaryEntity) list.push_back(em->tertiaryEntity);
  return ParaboxAPI::MakeArray(list);
}

} // namespace GameUtils
