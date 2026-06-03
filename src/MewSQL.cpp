#include "MewSQL.h"
#include "GameUtils.h"
#include <windows.h>
#include <cstring>

namespace MewSQL {

static SQLSaveFile_open_t g_open = nullptr;
static ExecSQL_t g_ExecSQL = nullptr;
static Retrieve_t g_Retrieve = nullptr;
static CloseConnection_t g_CloseConnection = nullptr;
static DestructString_t g_DestructString = nullptr;
static MsvcReleaseModeXString* g_baseSavePathPtr = nullptr;

void SetOpenPtr(const SQLSaveFile_open_t ptr) { g_open = ptr; }
void SetExecSQLPtr(const ExecSQL_t ptr) { g_ExecSQL = ptr; }
void SetRetrievePtr(const Retrieve_t ptr) { g_Retrieve = ptr; }
void SetCloseConnectionPtr(const CloseConnection_t ptr) { g_CloseConnection = ptr; }
void SetDestructStringPtr(const DestructString_t ptr) { g_DestructString = ptr; }
void SetBaseSavePathPtr(MsvcReleaseModeXString* ptr) { g_baseSavePathPtr = ptr; }

glaiel::SQLSaveFile* OpenSaveDatabase(const std::string& path) {
  if (!g_open) return nullptr;

  std::string fullPath = path;
  if (g_baseSavePathPtr && g_baseSavePathPtr->is_valid()) {
    std::string baseSavePath = g_baseSavePathPtr->begin();
    
    bool isAbsolute = false;
    if (path.length() >= 3 && path[1] == ':' && (path[2] == '\\' || path[2] == '/')) {
      isAbsolute = true;
    } else if (path.length() > 0 && (path[0] == '/' || path[0] == '\\')) {
      isAbsolute = true;
    } else if (!baseSavePath.empty() && path.find(baseSavePath) == 0) {
      isAbsolute = true;
    }

    if (!isAbsolute) {
      std::string cleanName = path;
      if (cleanName.find("saves/") == 0) {
        cleanName = cleanName.substr(6);
      } else if (cleanName.find("saves\\") == 0) {
        cleanName = cleanName.substr(6);
      }
      
      if (!baseSavePath.empty() && baseSavePath.back() != '/' && baseSavePath.back() != '\\') {
        baseSavePath += "/";
      }
      fullPath = baseSavePath + "saves/" + cleanName;
    }
  }

  auto* dbFile = new glaiel::SQLSaveFile();
  memset(dbFile, 0, sizeof(glaiel::SQLSaveFile));

  MsvcReleaseModeXString pathStr;
  GameUtils::InitXString(pathStr, fullPath);

  g_open(dbFile, &pathStr);
  return dbFile;
}

void CloseSaveDatabase(glaiel::SQLSaveFile* dbFile) {
  if (!dbFile) return;

  if (g_CloseConnection && dbFile->db) {
    g_CloseConnection(dbFile->db);
    dbFile->db = nullptr;
  }

  if (g_DestructString) {
    g_DestructString(&dbFile->db_path_string);
  }

  delete dbFile;
}

void ExecSQLOnDatabase(glaiel::SQLSaveFile* dbFile, const std::string& query) {
  if (!g_ExecSQL || !dbFile) return;

  MsvcReleaseModeXString queryStr;
  GameUtils::InitXString(queryStr, query);

  void* dummyFunc[8] = {}; // Dummy std::function block (64 bytes)
  g_ExecSQL(dbFile, &queryStr, dummyFunc);
}

int64_t ReadIntFromDatabase(glaiel::SQLSaveFile* dbFile, const std::string& key, int64_t defaultVal) {
  if (!g_Retrieve || !dbFile) return defaultVal;

  MsvcReleaseModeXString tableStr;
  GameUtils::InitXString(tableStr, "properties");

  MsvcReleaseModeXString keyStr;
  GameUtils::InitXString(keyStr, key);

  SQLData keyData;
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

} // namespace MewSQL
