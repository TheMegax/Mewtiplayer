#include "MewSQL.h"
#include "GameUtils.h"
#include "Overlay.h"
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

std::string GetAbsoluteSavePath(const std::string& path) {
  std::string fullPath = path;
  if (g_baseSavePathPtr && g_baseSavePathPtr->is_valid()) {
    std::string baseSavePath = g_baseSavePathPtr->begin();

    const bool isAbsolute = !path.empty() && (
      (path.size() >= 3 && path[1] == ':' && (path[2] == '\\' || path[2] == '/')) ||
      path[0] == '/' || path[0] == '\\' ||
      (!baseSavePath.empty() && path.rfind(baseSavePath, 0) == 0)
    );

    if (!isAbsolute) {
      std::string cleanName = path;
      if (cleanName.find("saves/") == 0 || cleanName.find("saves\\") == 0)
        cleanName = cleanName.substr(6);
      
      if (!baseSavePath.empty() && baseSavePath.back() != '/' && baseSavePath.back() != '\\')
        baseSavePath += "/";

      fullPath = baseSavePath + "saves/" + cleanName;
    }
  }
  return fullPath;
}

bool SaveFileExists(const std::string& path) {
  std::string fullPath = GetAbsoluteSavePath(path);
  DWORD dwAttrib = GetFileAttributesA(fullPath.c_str());
  return (dwAttrib != INVALID_FILE_ATTRIBUTES && !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}

void DeleteSaveFile(const std::string& path) {
  std::string fullPath = GetAbsoluteSavePath(path);
  BOOL ok = DeleteFileA(fullPath.c_str());
  if (!ok) {
    DWORD err = GetLastError();
    if (err != ERROR_FILE_NOT_FOUND) {
      Overlay::Log("[ERR] DeleteSaveFile '%s' failed, error %lu", fullPath.c_str(), err);
    }
  } else {
    Overlay::Log("[SAVE] Wiped save file: %s", fullPath.c_str());
  }
  DeleteFileA((fullPath + "-journal").c_str());
  DeleteFileA((fullPath + "-wal").c_str());
  DeleteFileA((fullPath + "-shm").c_str());
}

void CloseActiveSaveConnection(void* mewDirector) {
  if (!mewDirector || !g_CloseConnection) return;
  auto* saveFile = &static_cast<MewDirector*>(mewDirector)->sqlSaveFile;
  if (saveFile && saveFile->db) {
    g_CloseConnection(saveFile->db, 0);
    saveFile->db = nullptr;
  }
}

std::map<std::string, int> QueryMapFlags(const glaiel::SQLSaveFile* dbFile) {
  std::map<std::string, int> result;
  if (!dbFile || !dbFile->db) return result;
  if (!g_sqlite3Prepare || !g_sqlite3Step || !g_sqlite3ColumnText || !g_sqlite3Finalize) {
    Overlay::Log("[SAVE] Error: Missing sqlite3 function pointers for QueryMapFlags");
    return result;
  }

  const char* query = "SELECT key, data FROM properties WHERE key LIKE 'mapflag_%';";
  void* stmt = nullptr;
  int rc = g_sqlite3Prepare(dbFile->db, query, -1, 0x80, nullptr, &stmt, nullptr);
  
  if (rc != 0 || !stmt) {
    Overlay::Log("[SAVE] Error: sqlite3Prepare failed with code %d", rc);
    return result;
  }

  // 100 is SQLITE_ROW
  while (g_sqlite3Step(stmt) == 100) {
    const char* keyText = (const char*)g_sqlite3ColumnText(stmt, 0);
    const char* valText = (const char*)g_sqlite3ColumnText(stmt, 1);
    if (keyText && valText) {
      result[keyText] = std::stoi(valText);
    }
  }

  g_sqlite3Finalize(stmt);
  return result;
}

glaiel::SQLSaveFile* OpenSaveDatabase(const std::string& path) {
  if (!g_open) return nullptr;

  std::string fullPath = GetAbsoluteSavePath(path);

  auto* dbFile = new glaiel::SQLSaveFile();
  memset(dbFile, 0, sizeof(glaiel::SQLSaveFile));

  MsvcReleaseModeXString pathStr = {};
  GameUtils::InitXString(pathStr, fullPath);

  g_open(dbFile, &pathStr);
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

void ExecSQLOnDatabase(glaiel::SQLSaveFile* dbFile, const std::string& query) {
  if (!g_ExecSQL || !dbFile) return;

  MsvcReleaseModeXString queryStr = {};
  GameUtils::InitXString(queryStr, query);

  void* dummyFunc[8] = {}; // Dummy std::function block (64 bytes)
  g_ExecSQL(dbFile, &queryStr, dummyFunc);
}

int64_t ReadIntFromDatabase(glaiel::SQLSaveFile* dbFile, const std::string& key, int64_t defaultVal) {
  if (!g_Retrieve || !dbFile) return defaultVal;

  MsvcReleaseModeXString tableStr = {};
  GameUtils::InitXString(tableStr, "properties");

  MsvcReleaseModeXString keyStr = {};
  GameUtils::InitXString(keyStr, key);

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

std::vector<uint8_t> ReadBlobFromDatabase(glaiel::SQLSaveFile* dbFile, const std::string& table, const int64_t key) {
  std::vector<uint8_t> result;
  if (!g_Retrieve || !dbFile) return result;

  MsvcReleaseModeXString tableStr = {};
  GameUtils::InitXString(tableStr, table);

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
  return result;
}

std::vector<uint8_t> ReadBlobFromDatabaseStr(glaiel::SQLSaveFile* dbFile, const std::string& table, const std::string& key) {
  std::vector<uint8_t> result;
  if (!g_Retrieve || !dbFile) return result;

  MsvcReleaseModeXString tableStr = {};
  GameUtils::InitXString(tableStr, table);

  MsvcReleaseModeXString keyStr = {};
  GameUtils::InitXString(keyStr, key);

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
  return result;
}

void ExecSQLRaw(const glaiel::SQLSaveFile* dbFile, const std::string& query) {
  if (!dbFile || !dbFile->db || !g_sqlite3Prepare || !g_sqlite3Step || !g_sqlite3Finalize) return;
  void* stmt = nullptr;
  const int rc = g_sqlite3Prepare(dbFile->db, query.c_str(), -1, 0x80, nullptr, &stmt, nullptr);
  if (rc != 0 || !stmt) {
    Overlay::Log("[ERR] ExecSQLRaw sqlite3Prepare failed: code %d", rc);
    return;
  }
  g_sqlite3Step(stmt);
  g_sqlite3Finalize(stmt);
}

std::vector<uint8_t> ReadSaveFileRaw(const std::string& path) {
  const std::string fullPath = GetAbsoluteSavePath(path);
  std::vector<uint8_t> buffer;
  std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
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
  return buffer;
}

bool WriteSaveFileRaw(const std::string& path, const uint8_t* data, const size_t size) {
  const std::string fullPath = GetAbsoluteSavePath(path);
  std::ofstream file(fullPath, std::ios::binary);
  if (!file) {
    Overlay::Log("[ERR] Failed to open save file for writing: %s", fullPath.c_str());
    return false;
  }
  file.write((const char*)data, size); // NOLINT(*-narrowing-conversions)
  return file.good();
}

} // namespace MewSQL
