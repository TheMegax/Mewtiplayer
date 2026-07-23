#include "MewSQL.h"
#include "GameUtils.h"
#include "ParaboxAPI.h"
#include <windows.h>
#include <cstring>
#include <fstream>

namespace MewSQL {

static SQLSaveFile_open_t g_open = nullptr;
static ExecSQL_t g_ExecSQL = nullptr;
static Retrieve_t g_Retrieve = nullptr;
static CloseConnection_t g_CloseConnection = nullptr;
static DestructString_t g_DestructString = nullptr;
static MsvcReleaseModeXString* g_baseSavePathPtr = nullptr;

static sqlite3Prepare_t g_sqlite3Prepare = nullptr;
static sqlite3_step_t g_sqlite3Step = nullptr;
static sqlite3_column_text_t g_sqlite3ColumnText = nullptr;
static sqlite3_column_bytes_t g_sqlite3ColumnBytes = nullptr;
static sqlite3_finalize_t g_sqlite3Finalize = nullptr;

// ReSharper disable CppParameterMayBeConst
void SetOpenPtr(SQLSaveFile_open_t ptr) { g_open = ptr; }
void SetExecSQLPtr(ExecSQL_t ptr) { g_ExecSQL = ptr; }
void SetRetrievePtr(Retrieve_t ptr) { g_Retrieve = ptr; }
void SetCloseConnectionPtr(CloseConnection_t ptr) { g_CloseConnection = ptr; }
void SetDestructStringPtr(DestructString_t ptr) { g_DestructString = ptr; }
void SetBaseSavePathPtr(MsvcReleaseModeXString* ptr) { g_baseSavePathPtr = ptr; }

void SetSqlite3PreparePtr(sqlite3Prepare_t ptr) { g_sqlite3Prepare = ptr; }
void SetSqlite3StepPtr(sqlite3_step_t ptr) { g_sqlite3Step = ptr; }
void SetSqlite3ColumnTextPtr(sqlite3_column_text_t ptr) { g_sqlite3ColumnText = ptr; }
void SetSqlite3ColumnBytesPtr(sqlite3_column_bytes_t ptr) { g_sqlite3ColumnBytes = ptr; }
void SetSqlite3FinalizePtr(sqlite3_finalize_t ptr) { g_sqlite3Finalize = ptr; }
// ReSharper restore CppParameterMayBeConst

ParaboxAPI::String GetAbsoluteSavePath(const char* path) {
  std::string fullPath = path ? path : "";
  if (g_baseSavePathPtr && g_baseSavePathPtr->is_valid()) {
    std::string baseSavePath = g_baseSavePathPtr->begin();

    const bool isAbsolute = !fullPath.empty() && (
      (fullPath.size() >= 3 && fullPath[1] == ':' && (fullPath[2] == '\\' || fullPath[2] == '/')) ||
      fullPath[0] == '/' || fullPath[0] == '\\' ||
      (!baseSavePath.empty() && fullPath.rfind(baseSavePath, 0) == 0)
    );

    if (!isAbsolute) {
      std::string cleanName = fullPath;
      if (cleanName.find("saves/") == 0 || cleanName.find("saves\\") == 0)
        cleanName = cleanName.substr(6);
      
      if (!baseSavePath.empty() && baseSavePath.back() != '/' && baseSavePath.back() != '\\')
        baseSavePath += "/";

      fullPath = baseSavePath + "saves/" + cleanName;
    }
  }
  return ParaboxAPI::MakeString(fullPath);
}

bool SaveFileExists(const char* path) {
  ParaboxAPI::String fullPath = GetAbsoluteSavePath(path);
  DWORD dwAttrib = GetFileAttributesA(fullPath.c_str());
  return (dwAttrib != INVALID_FILE_ATTRIBUTES && !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}

void DeleteSaveFile(const char* path) {
  ParaboxAPI::String fullPath = GetAbsoluteSavePath(path);
  BOOL ok = DeleteFileA(fullPath.c_str());
  if (!ok) {
    DWORD err = GetLastError();
    if (err != ERROR_FILE_NOT_FOUND) {
      ParaboxAPI::Log("[ERR] DeleteSaveFile '%s' failed, error %lu", fullPath.c_str(), err);
    }
  } else {
    ParaboxAPI::Log("[SAVE] Wiped save file: %s", fullPath.c_str());
  }
  DeleteFileA((std::string(fullPath.c_str()) + "-journal").c_str());
  DeleteFileA((std::string(fullPath.c_str()) + "-wal").c_str());
  DeleteFileA((std::string(fullPath.c_str()) + "-shm").c_str());
}

void CloseActiveSaveConnection(void* mewDirector) {
  if (!mewDirector || !g_CloseConnection) return;
  auto* saveFile = &static_cast<MewDirector*>(mewDirector)->sqlSaveFile;
  if (saveFile && saveFile->db) {
    g_CloseConnection(saveFile->db, 0);
    saveFile->db = nullptr;
  }
}

ParaboxAPI::Array<SQLMapFlag> QueryMapFlags(const glaiel::SQLSaveFile* dbFile) {
  std::vector<SQLMapFlag> result;
  if (!dbFile || !dbFile->db) return ParaboxAPI::MakeArray(result);
  if (!g_sqlite3Prepare || !g_sqlite3Step || !g_sqlite3ColumnText || !g_sqlite3Finalize) {
    ParaboxAPI::Log("[SAVE] Error: Missing sqlite3 function pointers for QueryMapFlags");
    return ParaboxAPI::MakeArray(result);
  }

  const char* query = "SELECT key, data FROM properties WHERE key LIKE 'mapflag_%';";
  void* stmt = nullptr;
  int rc = g_sqlite3Prepare(dbFile->db, query, -1, 0x80, nullptr, &stmt, nullptr);
  
  if (rc != 0 || !stmt) {
    ParaboxAPI::Log("[SAVE] Error: sqlite3Prepare failed with code %d", rc);
    return ParaboxAPI::MakeArray(result);
  }

  // 100 is SQLITE_ROW
  while (g_sqlite3Step(stmt) == 100) {
    const char* keyText = (const char*)g_sqlite3ColumnText(stmt, 0);
    const char* valText = (const char*)g_sqlite3ColumnText(stmt, 1);
    if (keyText && valText) {
      SQLMapFlag flag;
      flag.key = ParaboxAPI::MakeString(keyText);
      flag.value = std::stoi(valText);
      result.push_back(std::move(flag));
    }
  }

  g_sqlite3Finalize(stmt);
  return ParaboxAPI::MakeArray(std::move(result));
}

glaiel::SQLSaveFile* OpenSaveDatabase(const char* path) {
  if (!g_open) return nullptr;

  ParaboxAPI::String fullPath = GetAbsoluteSavePath(path);

  auto* dbFile = new glaiel::SQLSaveFile();
  memset(dbFile, 0, sizeof(glaiel::SQLSaveFile));

  MsvcReleaseModeXString pathStr = {};
  GameUtils::InitXString(pathStr, fullPath.c_str());

  g_open(dbFile, &pathStr);

  if (!dbFile->db) {
    delete dbFile;
    return nullptr;
  }

  return dbFile;
}

void CloseSaveDatabase(glaiel::SQLSaveFile* dbFile) {
  if (!dbFile) return;

  if (g_CloseConnection && dbFile->db) {
    g_CloseConnection(dbFile->db, 0);
    dbFile->db = nullptr;
  }

  if (g_DestructString) {
    g_DestructString(&dbFile->db_path_string);
  }

  delete dbFile;
}

void ExecSQLOnDatabase(glaiel::SQLSaveFile* dbFile, const char* query) {
  if (!g_ExecSQL || !dbFile) return;

  MsvcReleaseModeXString queryStr = {};
  GameUtils::InitXString(queryStr, query ? query : "");

  void* dummyFunc[8] = {}; // Dummy std::function block (64 bytes)
  g_ExecSQL(dbFile, &queryStr, dummyFunc);
}

int64_t ReadIntFromDatabase(glaiel::SQLSaveFile* dbFile, const char* key, int64_t defaultVal) {
  if (!g_Retrieve || !dbFile) return defaultVal;

  MsvcReleaseModeXString tableStr = {};
  GameUtils::InitXString(tableStr, "properties");

  MsvcReleaseModeXString keyStr = {};
  GameUtils::InitXString(keyStr, key ? key : "");

  SQLData keyData = {};
  keyData.type = 2; // String
  keyData.padding = 0;
  keyData.intVal = (int64_t)keyStr.begin(); // Raw C-string character pointer
  keyData.length = (int64_t)keyStr.Mysize;  // String size

  SQLData outData = {};

  g_Retrieve(dbFile, &outData, &tableStr, &keyData, 1);

  int64_t result = defaultVal;
  if (outData.type == 5) {
    result = outData.intVal;
  }

  GameUtils::FreeXString(keyStr);
  return result;
}

ParaboxAPI::Array<uint8_t> ReadBlobFromDatabase(glaiel::SQLSaveFile* dbFile, const char* table, const int64_t key) {
  std::vector<uint8_t> result;
  if (!g_Retrieve || !dbFile) return ParaboxAPI::MakeArray(result);

  MsvcReleaseModeXString tableStr = {};
  GameUtils::InitXString(tableStr, table ? table : "");

  SQLData keyData = {};
  keyData.type = 5;
  keyData.padding = 0;
  keyData.intVal = key;
  keyData.length = 0;

  SQLData outData = {};

  g_Retrieve(dbFile, &outData, &tableStr, &keyData, 4);

  if (outData.intVal != 0 && outData.length > 0) {
    result.resize(outData.length);
    memcpy(result.data(), (const void*)outData.intVal, outData.length);
  }

  GameUtils::FreeXString(tableStr);
  return ParaboxAPI::MakeArray(result);
}

ParaboxAPI::Array<uint8_t> ReadBlobFromDatabaseStr(glaiel::SQLSaveFile* dbFile, const char* table, const char* key) {
  std::vector<uint8_t> result;
  if (!g_Retrieve || !dbFile) return ParaboxAPI::MakeArray(result);

  MsvcReleaseModeXString tableStr = {};
  GameUtils::InitXString(tableStr, table ? table : "");

  MsvcReleaseModeXString keyStr = {};
  GameUtils::InitXString(keyStr, key ? key : "");

  SQLData keyData = {};
  keyData.type = 2; // String
  keyData.padding = 0;
  keyData.intVal = (int64_t)keyStr.begin();
  keyData.length = (int64_t)keyStr.Mysize;

  SQLData outData = {};

  g_Retrieve(dbFile, &outData, &tableStr, &keyData, 4);

  if (outData.intVal != 0 && outData.length > 0) {
    result.resize(outData.length);
    memcpy(result.data(), (const void*)outData.intVal, outData.length);
  }

  GameUtils::FreeXString(keyStr);
  GameUtils::FreeXString(tableStr);
  return ParaboxAPI::MakeArray(result);
}

void ExecSQLRaw(const glaiel::SQLSaveFile* dbFile, const char* query) {
  if (!dbFile || !dbFile->db || !g_sqlite3Prepare || !g_sqlite3Step || !g_sqlite3Finalize || !query) return;
  void* stmt = nullptr;
  const int rc = g_sqlite3Prepare(dbFile->db, query, -1, 0x80, nullptr, &stmt, nullptr);
  if (rc != 0 || !stmt) {
    ParaboxAPI::Log("[ERR] ExecSQLRaw sqlite3Prepare failed: code %d", rc);
    return;
  }
  g_sqlite3Step(stmt);
  g_sqlite3Finalize(stmt);
}

ParaboxAPI::Array<uint8_t> ReadSaveFileRaw(const char* path) {
  ParaboxAPI::String fullPath = GetAbsoluteSavePath(path);
  std::vector<uint8_t> buffer;
  std::ifstream file(fullPath.c_str(), std::ios::binary | std::ios::ate);
  if (file) {
    const std::streamsize size = file.tellg();
    if (size > 0) {
      buffer.resize(size);
      file.seekg(0, std::ios::beg);
      if (!file.read((char*)buffer.data(), size)) {
        buffer.clear();
      }
    }
  }
  return ParaboxAPI::MakeArray(buffer);
}

bool WriteSaveFileRaw(const char* path, const uint8_t* data, const size_t size) {
  ParaboxAPI::String fullPath = GetAbsoluteSavePath(path);
  std::ofstream file(fullPath.c_str(), std::ios::binary);
  if (!file) {
    ParaboxAPI::Log("[ERR] Failed to open save file for writing: %s", fullPath.c_str());
    return false;
  }
  file.write((const char*)data, size); // NOLINT(*-narrowing-conversions)
  return file.good();
}

void CreateCatOwnershipTable(glaiel::SQLSaveFile* db) {
  ExecSQLRaw(db,
    "CREATE TABLE IF NOT EXISTS cat_ownership ("
    "  cat_slot      INTEGER PRIMARY KEY,"
    "  owner_steamid INTEGER NOT NULL,"
    "  cat_age       INTEGER NOT NULL DEFAULT 0"
    ");");
}

void WriteCatOwnershipEntry(glaiel::SQLSaveFile* db, const int32_t slot,
                             const uint64_t ownerSteamID, const int32_t catAge) {
  char query[256];
  snprintf(query, sizeof(query),
    "INSERT OR REPLACE INTO cat_ownership (cat_slot, owner_steamid, cat_age) "
    "VALUES (%d, %llu, %d);",
    slot, ownerSteamID, catAge);
  ExecSQLRaw(db, query);
}

CatOwnershipEntry ReadCatOwnershipEntry(glaiel::SQLSaveFile* db, const int32_t slot) {
  CatOwnershipEntry result = { 0, -1 };
  if (!db || !db->db) return result;
  if (!g_sqlite3Prepare || !g_sqlite3Step || !g_sqlite3ColumnText || !g_sqlite3Finalize) {
    ParaboxAPI::Log("[SAVE] [ERR] Missing sqlite3 pointers");
    return result;
  }

  char query[128];
  snprintf(query, sizeof(query),
    "SELECT owner_steamid, cat_age FROM cat_ownership WHERE cat_slot = %d;", slot);

  void* stmt = nullptr;
  const int rc = g_sqlite3Prepare(db->db, query, -1, 0x80, nullptr, &stmt, nullptr);
  if (rc != 0 || !stmt) return result;

  if (g_sqlite3Step(stmt) == 100) {
    const char* ownerText = (const char*)g_sqlite3ColumnText(stmt, 0);
    const char* ageText   = (const char*)g_sqlite3ColumnText(stmt, 1);
    if (ownerText) result.ownerSteamID = strtoull(ownerText, nullptr, 10);
    if (ageText)   result.catAge       = (int32_t)strtol(ageText, nullptr, 10);
  }

  g_sqlite3Finalize(stmt);
  return result;
}

} // namespace MewSQL
