#pragma once
#include "ParaboxAPI.h"
#include "ParaboxArray.h"
#include "MewgenicsTypes.h"
#include <string>
#include <vector>
#include <map>

struct SQLData {
  int32_t type;      // offset 0x0 (Type 5 = Int, Type 2 = String, Type 3 = Blob)
  int32_t padding;   // offset 0x4
  int64_t intVal;    // offset 0x8 (integer value or raw char* pointer)
  int64_t length;    // offset 0x10 (string/blob length)
};

namespace MewSQL {

// Signatures
typedef void (__fastcall *SQLSaveFile_open_t)(glaiel::SQLSaveFile* thisPtr, MsvcReleaseModeXString* pathStr);
typedef void (__fastcall *ExecSQL_t)(glaiel::SQLSaveFile* thisPtr, MsvcReleaseModeXString* queryStr, void* stdFuncCallback);
typedef void (__fastcall *Retrieve_t)(glaiel::SQLSaveFile* thisPtr, SQLData* outVal, MsvcReleaseModeXString* keyStr, SQLData* defaultVal, int32_t param5);
typedef int32_t (__fastcall *CloseConnection_t)(void* db, int32_t flag);
typedef void (__fastcall *DestructString_t)(MsvcReleaseModeXString* str);

typedef int (__fastcall *sqlite3Prepare_t)(void* db, const char* zSql, int nByte, unsigned int prepFlags, void* pVdbe, void** ppStmt, const char** pzTail);
typedef int (__fastcall *sqlite3_step_t)(void* stmt);
typedef const unsigned char* (__fastcall *sqlite3_column_text_t)(void* stmt, int iCol);
typedef int (__fastcall *sqlite3_column_bytes_t)(void* stmt, int iCol);
typedef int (__fastcall *sqlite3_finalize_t)(void* stmt);

PARABOX_API void SetOpenPtr(SQLSaveFile_open_t ptr);
PARABOX_API void SetExecSQLPtr(ExecSQL_t ptr);
PARABOX_API void SetRetrievePtr(Retrieve_t ptr);
PARABOX_API void SetCloseConnectionPtr(CloseConnection_t ptr);
PARABOX_API void SetDestructStringPtr(DestructString_t ptr);
PARABOX_API void SetBaseSavePathPtr(MsvcReleaseModeXString* ptr);

PARABOX_API void SetSqlite3PreparePtr(sqlite3Prepare_t ptr);
PARABOX_API void SetSqlite3StepPtr(sqlite3_step_t ptr);
PARABOX_API void SetSqlite3ColumnTextPtr(sqlite3_column_text_t ptr);
PARABOX_API void SetSqlite3ColumnBytesPtr(sqlite3_column_bytes_t ptr);
PARABOX_API void SetSqlite3FinalizePtr(sqlite3_finalize_t ptr);

struct SQLMapFlag {
    ParaboxAPI::String key;
    int value;
};

PARABOX_API ParaboxAPI::Array<SQLMapFlag> QueryMapFlags(const glaiel::SQLSaveFile* dbFile);
PARABOX_API glaiel::SQLSaveFile* OpenSaveDatabase(const char* path);
PARABOX_API void CloseSaveDatabase(glaiel::SQLSaveFile* dbFile);
PARABOX_API void ExecSQLOnDatabase(glaiel::SQLSaveFile* dbFile, const char* query);
PARABOX_API void ExecSQLRaw(const glaiel::SQLSaveFile* dbFile, const char* query);
PARABOX_API int64_t ReadIntFromDatabase(glaiel::SQLSaveFile* dbFile, const char* key, int64_t defaultVal = 0);
PARABOX_API ParaboxAPI::Array<uint8_t> ReadBlobFromDatabase(glaiel::SQLSaveFile* dbFile, const char* table, int64_t key);
PARABOX_API ParaboxAPI::Array<uint8_t> ReadBlobFromDatabaseStr(glaiel::SQLSaveFile* dbFile, const char* table, const char* key);
PARABOX_API ParaboxAPI::Array<uint8_t> ReadSaveFileRaw(const char* path);
PARABOX_API bool WriteSaveFileRaw(const char* path, const uint8_t* data, size_t size);
PARABOX_API ParaboxAPI::String GetAbsoluteSavePath(const char* path);
PARABOX_API bool SaveFileExists(const char* path);
PARABOX_API void DeleteSaveFile(const char* path);
PARABOX_API void CloseActiveSaveConnection(void* mewDirector);

} // namespace MewSQL
