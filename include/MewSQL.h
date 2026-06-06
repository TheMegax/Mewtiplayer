#pragma once
#include "MewgenicsTypes.h"
#include <string>

struct SQLData {
  int32_t type;      // offset 0x0 (Type 5 = Int, Type 2 = String, Type 3 = Blob)
  int32_t padding;   // offset 0x4
  int64_t intVal;    // offset 0x8 (integer value or raw char* pointer)
  int64_t length;    // offset 0x10 (string/blob length)
};

#include <vector>

namespace glaiel {
struct SQLSaveFile {
  void* db;                             // 0x00
  MsvcReleaseModeXString db_path_string; // 0x08
  char padding[128];                    // 0x28 (safe padding)
};
}

namespace MewSQL {

// Signatures
typedef void (__fastcall *SQLSaveFile_open_t)(glaiel::SQLSaveFile* thisPtr, MsvcReleaseModeXString* pathStr);
typedef void (__fastcall *ExecSQL_t)(glaiel::SQLSaveFile* thisPtr, MsvcReleaseModeXString* queryStr, void* stdFuncCallback);
typedef void (__fastcall *Retrieve_t)(glaiel::SQLSaveFile* thisPtr, SQLData* outVal, MsvcReleaseModeXString* keyStr, SQLData* defaultVal, int32_t param5);
typedef int32_t (__fastcall *CloseConnection_t)(void* db, int32_t flag);
typedef void (__fastcall *DestructString_t)(MsvcReleaseModeXString* str);

void SetOpenPtr(SQLSaveFile_open_t ptr);
void SetExecSQLPtr(ExecSQL_t ptr);
void SetRetrievePtr(Retrieve_t ptr);
void SetCloseConnectionPtr(CloseConnection_t ptr);
void SetDestructStringPtr(DestructString_t ptr);
void SetBaseSavePathPtr(MsvcReleaseModeXString* ptr);

glaiel::SQLSaveFile* OpenSaveDatabase(const std::string& path);
void CloseSaveDatabase(glaiel::SQLSaveFile* dbFile);
void ExecSQLOnDatabase(glaiel::SQLSaveFile* dbFile, const std::string& query);
int64_t ReadIntFromDatabase(glaiel::SQLSaveFile* dbFile, const std::string& key, int64_t defaultVal = 0);
std::vector<uint8_t> ReadBlobFromDatabase(glaiel::SQLSaveFile* dbFile, const std::string& table, int64_t key);
std::vector<uint8_t> ReadSaveFileRaw(const std::string& path);
bool WriteSaveFileRaw(const std::string& path, const uint8_t* data, size_t size);
std::string GetAbsoluteSavePath(const std::string& path);
bool SaveFileExists(const std::string& path);
void DeleteSaveFile(const std::string& path);
void CloseActiveSaveConnection(void* mewDirector);

} // namespace MewSQL
