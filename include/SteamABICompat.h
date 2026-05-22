#pragma once
#include "steam_api.h"
#include "steam_api_flat.h"

// Define compatibility wrappers for Steam interfaces to avoid ABI mismatch
// when calling virtual methods that take/return CSteamID or other classes by value.

class ISteamUserCompat {
public:
    inline CSteamID GetSteamID() {
        return CSteamID(SteamAPI_ISteamUser_GetSteamID(SteamAPI_SteamUser()));
    }
};

class ISteamFriendsCompat {
public:
    inline const char *GetFriendPersonaName(CSteamID steamIDFriend) {
        return SteamAPI_ISteamFriends_GetFriendPersonaName(SteamAPI_SteamFriends(), steamIDFriend.ConvertToUint64());
    }
};

class ISteamNetworkingCompat {
public:
    inline bool SendP2PPacket(CSteamID steamIDRemote, const void *pubData, uint32 cubData, EP2PSend eP2PSendType, int nChannel = 0) {
        return SteamAPI_ISteamNetworking_SendP2PPacket(SteamAPI_SteamNetworking(), steamIDRemote.ConvertToUint64(), pubData, cubData, eP2PSendType, nChannel);
    }
    inline bool IsP2PPacketAvailable(uint32 *pcubMsgSize, int nChannel = 0) {
        return SteamAPI_ISteamNetworking_IsP2PPacketAvailable(SteamAPI_SteamNetworking(), pcubMsgSize, nChannel);
    }
    inline bool ReadP2PPacket(void *pubDest, uint32 cubDest, uint32 *pcubMsgSize, CSteamID *psteamIDRemote, int nChannel = 0) {
        return SteamAPI_ISteamNetworking_ReadP2PPacket(SteamAPI_SteamNetworking(), pubDest, cubDest, pcubMsgSize, psteamIDRemote, nChannel);
    }
    inline bool AcceptP2PSessionWithUser(CSteamID steamIDRemote) {
        return SteamAPI_ISteamNetworking_AcceptP2PSessionWithUser(SteamAPI_SteamNetworking(), steamIDRemote.ConvertToUint64());
    }
};

class ISteamMatchmakingCompat {
public:
    inline CSteamID GetLobbyOwner(CSteamID steamIDLobby) {
        return CSteamID(SteamAPI_ISteamMatchmaking_GetLobbyOwner(SteamAPI_SteamMatchmaking(), steamIDLobby.ConvertToUint64()));
    }
    inline SteamAPICall_t CreateLobby(ELobbyType eLobbyType, int cMaxMembers) {
        return SteamAPI_ISteamMatchmaking_CreateLobby(SteamAPI_SteamMatchmaking(), eLobbyType, cMaxMembers);
    }
    inline void LeaveLobby(CSteamID steamIDLobby) {
        SteamAPI_ISteamMatchmaking_LeaveLobby(SteamAPI_SteamMatchmaking(), steamIDLobby.ConvertToUint64());
    }
    inline SteamAPICall_t JoinLobby(CSteamID steamIDLobby) {
        return SteamAPI_ISteamMatchmaking_JoinLobby(SteamAPI_SteamMatchmaking(), steamIDLobby.ConvertToUint64());
    }
    inline void AddRequestLobbyListDistanceFilter(ELobbyDistanceFilter eLobbyDistanceFilter) {
        SteamAPI_ISteamMatchmaking_AddRequestLobbyListDistanceFilter(SteamAPI_SteamMatchmaking(), eLobbyDistanceFilter);
    }
    inline void AddRequestLobbyListStringFilter(const char *pchKeyToMatch, const char *pchValueToMatch, ELobbyComparison eComparisonType) {
        SteamAPI_ISteamMatchmaking_AddRequestLobbyListStringFilter(SteamAPI_SteamMatchmaking(), pchKeyToMatch, pchValueToMatch, eComparisonType);
    }
    inline SteamAPICall_t RequestLobbyList() {
        return SteamAPI_ISteamMatchmaking_RequestLobbyList(SteamAPI_SteamMatchmaking());
    }
    inline int GetNumLobbyMembers(CSteamID steamIDLobby) {
        return SteamAPI_ISteamMatchmaking_GetNumLobbyMembers(SteamAPI_SteamMatchmaking(), steamIDLobby.ConvertToUint64());
    }
    inline CSteamID GetLobbyMemberByIndex(CSteamID steamIDLobby, int iMember) {
        return CSteamID(SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex(SteamAPI_SteamMatchmaking(), steamIDLobby.ConvertToUint64(), iMember));
    }
    inline bool SetLobbyData(CSteamID steamIDLobby, const char *pchKey, const char *pchValue) {
        return SteamAPI_ISteamMatchmaking_SetLobbyData(SteamAPI_SteamMatchmaking(), steamIDLobby.ConvertToUint64(), pchKey, pchValue);
    }
    inline const char *GetLobbyData(CSteamID steamIDLobby, const char *pchKey) {
        return SteamAPI_ISteamMatchmaking_GetLobbyData(SteamAPI_SteamMatchmaking(), steamIDLobby.ConvertToUint64(), pchKey);
    }
    inline int GetLobbyMemberLimit(CSteamID steamIDLobby) {
        return SteamAPI_ISteamMatchmaking_GetLobbyMemberLimit(SteamAPI_SteamMatchmaking(), steamIDLobby.ConvertToUint64());
    }
    inline CSteamID GetLobbyByIndex(int iLobby) {
        return CSteamID(SteamAPI_ISteamMatchmaking_GetLobbyByIndex(SteamAPI_SteamMatchmaking(), iLobby));
    }
};

inline ISteamUserCompat* GetSteamUserCompat() {
    static ISteamUserCompat instance;
    return &instance;
}

inline ISteamFriendsCompat* GetSteamFriendsCompat() {
    static ISteamFriendsCompat instance;
    return &instance;
}

inline ISteamMatchmakingCompat* GetSteamMatchmakingCompat() {
    static ISteamMatchmakingCompat instance;
    return &instance;
}

inline ISteamNetworkingCompat* GetSteamNetworkingCompat() {
    static ISteamNetworkingCompat instance;
    return &instance;
}

// Redirect standard SDK interface accessors to our compat wrapper objects
#define SteamUser() GetSteamUserCompat()
#define SteamFriends() GetSteamFriendsCompat()
#define SteamMatchmaking() GetSteamMatchmakingCompat()
#define SteamNetworking() GetSteamNetworkingCompat()
