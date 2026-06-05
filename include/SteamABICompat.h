#pragma once
#include "steam_api.h"
#include "steam_api_flat.h"

// Define compatibility wrappers for Steam interfaces to avoid ABI mismatch
// when calling virtual methods that take/return CSteamID or other classes by value.

class ISteamUserCompat {
public:
    static CSteamID GetSteamID() {
        return CSteamID(SteamAPI_ISteamUser_GetSteamID(SteamAPI_SteamUser()));
    }
};

class ISteamFriendsCompat {
public:
    static const char *GetFriendPersonaName(const CSteamID steamIDFriend) {
        return SteamAPI_ISteamFriends_GetFriendPersonaName(SteamAPI_SteamFriends(), steamIDFriend.ConvertToUint64());
    }
};

class ISteamNetworkingCompat {
public:
    static bool SendP2PPacket(const CSteamID steamIDRemote, const void *pubData, const uint32 cubData, const EP2PSend eP2PSendType, const int nChannel = 0) {
        return SteamAPI_ISteamNetworking_SendP2PPacket(SteamAPI_SteamNetworking(), steamIDRemote.ConvertToUint64(), pubData, cubData, eP2PSendType, nChannel);
    }

    static bool IsP2PPacketAvailable(uint32 *pcubMsgSize, const int nChannel = 0) {
        return SteamAPI_ISteamNetworking_IsP2PPacketAvailable(SteamAPI_SteamNetworking(), pcubMsgSize, nChannel);
    }

    static bool ReadP2PPacket(void *pubDest, const uint32 cubDest, uint32 *pcubMsgSize, CSteamID *psteamIDRemote, const int nChannel = 0) {
        return SteamAPI_ISteamNetworking_ReadP2PPacket(SteamAPI_SteamNetworking(), pubDest, cubDest, pcubMsgSize, psteamIDRemote, nChannel);
    }

    static bool AcceptP2PSessionWithUser(const CSteamID steamIDRemote) {
        return SteamAPI_ISteamNetworking_AcceptP2PSessionWithUser(SteamAPI_SteamNetworking(), steamIDRemote.ConvertToUint64());
    }
};

class ISteamMatchmakingCompat {
public:
    static CSteamID GetLobbyOwner(const CSteamID steamIDLobby) {
        return CSteamID(SteamAPI_ISteamMatchmaking_GetLobbyOwner(SteamAPI_SteamMatchmaking(), steamIDLobby.ConvertToUint64()));
    }

    static SteamAPICall_t CreateLobby(const ELobbyType eLobbyType, const int cMaxMembers) {
        return SteamAPI_ISteamMatchmaking_CreateLobby(SteamAPI_SteamMatchmaking(), eLobbyType, cMaxMembers);
    }

    static void LeaveLobby(const CSteamID steamIDLobby) {
        SteamAPI_ISteamMatchmaking_LeaveLobby(SteamAPI_SteamMatchmaking(), steamIDLobby.ConvertToUint64());
    }

    static SteamAPICall_t JoinLobby(const CSteamID steamIDLobby) {
        return SteamAPI_ISteamMatchmaking_JoinLobby(SteamAPI_SteamMatchmaking(), steamIDLobby.ConvertToUint64());
    }

    static void AddRequestLobbyListDistanceFilter(const ELobbyDistanceFilter eLobbyDistanceFilter) {
        SteamAPI_ISteamMatchmaking_AddRequestLobbyListDistanceFilter(SteamAPI_SteamMatchmaking(), eLobbyDistanceFilter);
    }

    static void AddRequestLobbyListStringFilter(const char *pchKeyToMatch, const char *pchValueToMatch, const ELobbyComparison eComparisonType) {
        SteamAPI_ISteamMatchmaking_AddRequestLobbyListStringFilter(SteamAPI_SteamMatchmaking(), pchKeyToMatch, pchValueToMatch, eComparisonType);
    }

    static SteamAPICall_t RequestLobbyList() {
        return SteamAPI_ISteamMatchmaking_RequestLobbyList(SteamAPI_SteamMatchmaking());
    }

    static int GetNumLobbyMembers(const CSteamID steamIDLobby) {
        return SteamAPI_ISteamMatchmaking_GetNumLobbyMembers(SteamAPI_SteamMatchmaking(), steamIDLobby.ConvertToUint64());
    }

    static CSteamID GetLobbyMemberByIndex(const CSteamID steamIDLobby, const int iMember) {
        return CSteamID(SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex(SteamAPI_SteamMatchmaking(), steamIDLobby.ConvertToUint64(), iMember));
    }

    static bool SetLobbyData(const CSteamID steamIDLobby, const char *pchKey, const char *pchValue) {
        return SteamAPI_ISteamMatchmaking_SetLobbyData(SteamAPI_SteamMatchmaking(), steamIDLobby.ConvertToUint64(), pchKey, pchValue);
    }

    static const char *GetLobbyData(const CSteamID steamIDLobby, const char *pchKey) {
        return SteamAPI_ISteamMatchmaking_GetLobbyData(SteamAPI_SteamMatchmaking(), steamIDLobby.ConvertToUint64(), pchKey);
    }

    static int GetLobbyMemberLimit(const CSteamID steamIDLobby) {
        return SteamAPI_ISteamMatchmaking_GetLobbyMemberLimit(SteamAPI_SteamMatchmaking(), steamIDLobby.ConvertToUint64());
    }

    static CSteamID GetLobbyByIndex(const int iLobby) {
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
