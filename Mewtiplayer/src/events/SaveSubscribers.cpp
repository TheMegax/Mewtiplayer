#include "events/SaveSubscribers.h"
#include "ParaboxAPI.h"
#include "NetworkManager.h"
#include "Overlay.h"
#include "GameUtils.h"
#include "MewSQL.h"
#include "MewgenicsTypes.h"

// SaveHooks state
std::map<int64_t, uint64_t> g_catIdToOwnerSteamID;

bool g_isHandlingNetworkMapInventoryOpen = false;

// ---------------------------------------------------------------------------
// SaveHooks Subscribers
// ---------------------------------------------------------------------------
void RegisterSaveSubscribers() {
    ParaboxAPI::OnCreateStrayCat.Subscribe([](ParaboxAPI::CreateStrayCatEvent& ev) {
        void* cat = ev.returnValue;
        if (GameUtils::g_isLoadingCustomCats && cat) {
            const int index = GameUtils::g_currentCustomCatIndex;
            Overlay::Log("[SAVE] Hooked CreateStrayCat, loading custom cat at index %d from %s...", index, "mewtiplayer.sav");

            if (SQLSaveFile* dbFile = MewSQL::OpenSaveDatabase("mewtiplayer.sav")) {
                if (GameUtils::MewSaveFile_Load_t mewSaveFileLoad = GameUtils::GetMewSaveFileLoadPtr()) {
                    MewSaveFile dummySave = {};
                    dummySave.db = *dbFile;

                    const auto catIdPtr = &((CatData*)cat)->cat_uid;
                    const int64_t originalID = *catIdPtr;

                    mewSaveFileLoad(&dummySave, index, cat);
                    *catIdPtr = originalID;

                    const int64_t catUID = ((CatData*)cat)->cat_uid;

                    const int slot = GameUtils::g_currentCustomCatIndex; // 1, 2, ...
                    const auto [ownerSteamID, catAge] = MewSQL::ReadCatOwnershipEntry(dbFile, slot);

                    if (ownerSteamID != 0) {
                        g_catIdToOwnerSteamID[catUID] = ownerSteamID;
                        NetworkManager::Get().GetOwnershipMap()[catUID] = ownerSteamID;
                        Overlay::Log("[SAVE] CreateStrayCat: OK slot=%d cat_uid=%lld -> owner=%llu age=%d",
                                     slot, catUID, ownerSteamID, catAge);
                    } else {
                        Overlay::Log("[SAVE] [WARN] CreateStrayCat: No owner for slot %d (cat_uid=%lld)", slot, catUID);
                    }

                    std::string narrowNameStr = ((CatData*)cat)->name_.to_utf8();
                    NetworkManager::Get().RegisterCat(catUID, narrowNameStr.c_str(), ((CatData*)cat)->cat_class.begin());

                    if (catAge > 0) {
                        const MewDirector* dir = GameUtils::GetMewDirectorSingleton();
                        const int32_t currentDayVal = dir ? static_cast<int32_t>(dir->current_day) : 1;
                        ((CatData*)cat)->birthday = currentDayVal - catAge;
                        Overlay::Log("[SAVE] Restored age %d for cat_uid=%lld", catAge, catUID);
                    }
                }
                MewSQL::CloseSaveDatabase(dbFile);
            }
            GameUtils::g_currentCustomCatIndex++;
        }
    });

    ParaboxAPI::OnGetCollarVector.Subscribe([](ParaboxAPI::GetCollarVectorEvent& ev) {
        if (!GameUtils::g_useCustomCollarClasses || GameUtils::g_customCollarClasses.empty()) {
            return;
        }

        Overlay::Log("[SAVE] Populating custom collar vector with %zu classes...", GameUtils::g_customCollarClasses.size());

        const size_t count = GameUtils::g_customCollarClasses.size();
        const size_t bytesToAllocate = count * sizeof(MsvcReleaseModeXString);

        ev.outVector[0] = 0;
        ev.outVector[1] = 0;
        ev.outVector[2] = 0;

        if (auto* array = (MsvcReleaseModeXString*)ParaboxAPI::GameAllocate(bytesToAllocate)) {
            memset(array, 0, bytesToAllocate);
            for (size_t i = 0; i < count; ++i) {
                GameUtils::InitXString(array[i], GameUtils::g_customCollarClasses[i].c_str());
            }
            ev.outVector[0] = (int64_t)array;
            ev.outVector[1] = (int64_t)(array + count);
            ev.outVector[2] = (int64_t)(array + count);
            ev.returnValue = ev.outVector;
            ev.Cancel(); // Prevent calling original
        }
    });
}
