#include "MewSQL.h"
#include "GameUtils.h"
#include "Overlay.h"
#include "hooks/ModState.h"
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

void SetOpenPtr(const SQLSaveFile_open_t ptr) { g_open = ptr; }
void SetExecSQLPtr(const ExecSQL_t ptr) { g_ExecSQL = ptr; }
void SetRetrievePtr(const Retrieve_t ptr) { g_Retrieve = ptr; }
void SetCloseConnectionPtr(const CloseConnection_t ptr) { g_CloseConnection = ptr; }
void SetDestructStringPtr(const DestructString_t ptr) { g_DestructString = ptr; }
void SetBaseSavePathPtr(MsvcReleaseModeXString* ptr) { g_baseSavePathPtr = ptr; }

std::string GetAbsoluteSavePath(const std::string& path) {
  std::string fullPath = path;
  if (g_baseSavePathPtr && g_baseSavePathPtr->is_valid()) {
    std::string baseSavePath = g_baseSavePathPtr->begin();
    
    bool isAbsolute = false;
    if (path.length() >= 3 && path[1] == ':' && (path[2] == '\\' || path[2] == '/'))
      isAbsolute = true;
    else if (!path.empty() && (path[0] == '/' || path[0] == '\\')) {
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
  if (!mewDirector) return;
  void** dbPtr = &((MewDirector*)mewDirector)->sqlSaveFile;
  if (dbPtr && *dbPtr && g_CloseConnection) {
    Overlay::Log("[SAVE] Closing active director SQL database connection to unlock save file...");
    g_CloseConnection(*dbPtr, 0);
    *dbPtr = nullptr;
  }
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

  if (g_modState.talkative) {
    Overlay::Log("[SAVE_DEBUG] ReadBlobFromDatabase table=%s key=%lld -> returned type=%d, intVal=%lld, length=%lld",
                 table.c_str(), key, outData.type, outData.intVal, outData.length);
  }

  if (outData.intVal != 0 && outData.length > 0) {
    result.resize(outData.length);
    memcpy(result.data(), (const void*)outData.intVal, outData.length);
  }

  GameUtils::FreeXString(tableStr);
  return result;
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

bool WriteSaveFileRaw(const std::string& path, const uint8_t* data, size_t size) {
  const std::string fullPath = GetAbsoluteSavePath(path);
  std::ofstream file(fullPath, std::ios::binary);
  if (!file) {
    Overlay::Log("[ERR] Failed to open save file for writing: %s", fullPath.c_str());
    return false;
  }
  file.write((const char*)data, size);
  return file.good();
}

} // namespace MewSQL
