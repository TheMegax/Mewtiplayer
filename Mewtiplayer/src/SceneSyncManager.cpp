#include "SceneSyncManager.h"
#include "ModState.h"
#include "Overlay.h"
#include "GameUtils.h"
#include <cstring>

static bool IsSyncedRunScene(const std::string &sceneName) {
  return (sceneName == "Battle" ||
          sceneName == "Combat" ||
          sceneName == "Shop" ||
          sceneName == "Event" ||
          sceneName == "LevelUp" ||
          sceneName == "Map" ||
          sceneName == "ActSelection" ||
          sceneName == "Interstitial");
}

void SceneSyncManager::Init() {
  ParaboxAPI::OnSceneAdded.Subscribe([](ParaboxAPI::SceneAddedEvent &ev) {
    if (ev.scene && ev.sceneName) {
      SceneSyncManager::Get().OnSceneAdded(static_cast<Scene*>(ev.scene), ev.sceneName);
    }
  });

  ParaboxAPI::OnResumeMap.Subscribe([](ParaboxAPI::ResumeMapEvent &ev) {
    SceneSyncManager::Get().OnResumeMap(static_cast<Scene*>(ev.mapScene));
  });

  Overlay::Log("[BARRIER] Initialized");
}

void SceneSyncManager::OnSceneAdded(Scene *scene, const char *sceneName) {
  if (!scene || !sceneName) return;
  const std::string name(sceneName);
  Overlay::Log("[BARRIER] Scene added: %s", name.c_str());
  if (!IsSyncedRunScene(name)) return;

  StartBarrier(scene, name);
}

void SceneSyncManager::OnResumeMap(Scene *mapScene) {
  Overlay::Log("[BARRIER] Resuming map");
  StartBarrier(mapScene, "Map");
}

void SceneSyncManager::StartBarrier(Scene *scene, const std::string &sceneName) {
  const CSteamID lobby = NetworkManager::Get().GetCurrentLobby();
  if (!lobby.IsValid()) {
    return;
  }

  const int numMembers = SteamMatchmaking()->GetNumLobbyMembers(lobby);
  const uint64_t delayMs = (g_modState.networkSimulation && g_modState.simSceneLoadDelaySeconds > 0)
                               ? static_cast<uint64_t>(g_modState.simSceneLoadDelaySeconds) * 1000
                               : 0;

  // Don't pause if alone and not simulating a delay
  if (numMembers <= 1 && delayMs == 0) {
    return;
  }

  if (m_waitingForPeers && m_currentSceneName == sceneName && m_currentScenePtr == scene) {
    return;
  }

  m_currentBarrierId++;
  m_currentSceneName = sceneName;
  m_currentScenePtr = scene;
  m_waitingForPeers = true;
  m_barrierStartTime = GetTickCount64();

  if (m_currentScenePtr) {
    GameUtils::SetScenePaused(m_currentScenePtr, true, true);
  }
  Overlay::Log("[BARRIER] Pausing '%s' until all players load", m_currentSceneName.c_str());

  if (delayMs > 0) {
    m_readySignalPending = true;
    m_readySignalSendTime = GetTickCount64() + delayMs;
    Overlay::Log("[BARRIER] Simulating %ds load delay for '%s'",
                 g_modState.simSceneLoadDelaySeconds, m_currentSceneName.c_str());
  } else {
    m_readySignalPending = false;
    SendReadySignal();
  }
}

void SceneSyncManager::SendReadySignal() {
  const uint64_t mySteamID = SteamUser()->GetSteamID().ConvertToUint64();
  m_peerReadyForScene[mySteamID] = m_currentSceneName;

  SceneLoadReadyPacket pkt = {};
  pkt.barrierId = m_currentBarrierId;
  strncpy(pkt.sceneName, m_currentSceneName.c_str(), sizeof(pkt.sceneName) - 1);

  NetworkManager::Get().BroadcastPacket(PacketType::SceneLoadReady, &pkt, sizeof(pkt), true);
  Overlay::Log("[BARRIER] Local ready for '%s'", m_currentSceneName.c_str());

  if (NetworkManager::Get().IsHost()) {
    CheckAllPeersReady();
  }
}

void SceneSyncManager::HandleSceneLoadReady(uint64_t senderSteamID, const SceneLoadReadyPacket *pkt) {
  if (!pkt) return;

  m_peerReadyForScene[senderSteamID] = pkt->sceneName;
  Overlay::Log("[BARRIER] Player %llu ready for '%s'", senderSteamID, pkt->sceneName);

  if (NetworkManager::Get().IsHost()) {
    CheckAllPeersReady();
  }
}

void SceneSyncManager::CheckAllPeersReady() {
  if (!NetworkManager::Get().IsHost()) return;
  if (!m_waitingForPeers) return;
  if (m_readySignalPending) return;

  const CSteamID lobby = NetworkManager::Get().GetCurrentLobby();
  if (!lobby.IsValid()) return;

  const int numMembers = SteamMatchmaking()->GetNumLobbyMembers(lobby);
  if (numMembers <= 1) {
    ProceedBarrier();
    return;
  }

  bool allReady = true;
  for (int i = 0; i < numMembers; i++) {
    const uint64_t memberID = SteamMatchmaking()->GetLobbyMemberByIndex(lobby, i).ConvertToUint64();
    const auto it = m_peerReadyForScene.find(memberID);
    if (it == m_peerReadyForScene.end() || it->second != m_currentSceneName) {
      allReady = false;
      break;
    }
  }

  if (allReady) {
    ProceedBarrier();
  }
}

void SceneSyncManager::HandleSceneLoadProceed(const SceneLoadProceedPacket *pkt) {
  if (!pkt) return;
  Overlay::Log("[BARRIER] Proceeding '%s'", pkt->sceneName);
  ProceedBarrier();
}

void SceneSyncManager::ProceedBarrier() {
  if (!m_waitingForPeers) return;

  if (NetworkManager::Get().IsHost()) {
    SceneLoadProceedPacket pkt = {};
    pkt.barrierId = m_currentBarrierId;
    strncpy(pkt.sceneName, m_currentSceneName.c_str(), sizeof(pkt.sceneName) - 1);
    NetworkManager::Get().BroadcastPacket(PacketType::SceneLoadProceed, &pkt, sizeof(pkt), true);
    Overlay::Log("[BARRIER] All players loaded '%s', resuming", m_currentSceneName.c_str());
  }

  Scene *activeScene = m_currentScenePtr;
  if (!activeScene) {
    activeScene = GameUtils::GetSceneByName(m_currentSceneName.c_str());
  }

  if (activeScene) {
    GameUtils::SetScenePaused(activeScene, false, false);
    Overlay::Log("[BARRIER] Resumed '%s'", m_currentSceneName.c_str());
  }

  m_waitingForPeers = false;
  m_readySignalPending = false;
  m_peerReadyForScene.clear();
}

void SceneSyncManager::ForceProceed() {
  Overlay::Log("[BARRIER] Force resuming '%s'", m_currentSceneName.c_str());
  ProceedBarrier();
}

void SceneSyncManager::Update() {
  if (m_readySignalPending && GetTickCount64() >= m_readySignalSendTime) {
    m_readySignalPending = false;
    SendReadySignal();
  }

  if (m_waitingForPeers) {
    Scene *activeScene = m_currentScenePtr;
    if (!activeScene) {
      activeScene = GameUtils::GetSceneByName(m_currentSceneName.c_str());
      m_currentScenePtr = activeScene;
    }
    if (activeScene && !activeScene->doing_scene_destruction) {
      activeScene->paused = true;
      activeScene->controls_prevented = true;
    }

    if (NetworkManager::Get().IsHost()) {
      CheckAllPeersReady();
    }

    // Safety timeout in case a peer disconnects or crashes while loading
    if (GetTickCount64() - m_barrierStartTime > BARRIER_TIMEOUT_MS) {
      Overlay::Log("[BARRIER] Timed out waiting for players in '%s', resuming", m_currentSceneName.c_str());
      ForceProceed();
    }
  }
}

std::vector<SceneSyncManager::PeerStatus> SceneSyncManager::GetPeerStatuses() const {
  std::vector<PeerStatus> result;
  const CSteamID lobby = NetworkManager::Get().GetCurrentLobby();
  if (!lobby.IsValid()) return result;

  const int numMembers = SteamMatchmaking()->GetNumLobbyMembers(lobby);
  result.reserve(numMembers);

  const uint64_t mySteamID = SteamUser()->GetSteamID().ConvertToUint64();

  for (int i = 0; i < numMembers; i++) {
    const uint64_t memberID = SteamMatchmaking()->GetLobbyMemberByIndex(lobby, i).ConvertToUint64();
    std::string name = NetworkManager::Get().GetCachedPlayerName(memberID);
    if (name.empty()) {
      name = (memberID == mySteamID) ? "You" : "Player";
    }

    bool ready = false;
    if (memberID == mySteamID) {
      ready = !m_readySignalPending;
    } else {
      const auto it = m_peerReadyForScene.find(memberID);
      if (it != m_peerReadyForScene.end() && it->second == m_currentSceneName) {
        ready = true;
      }
    }

    result.push_back(PeerStatus{memberID, name, ready});
  }

  return result;
}

void SceneSyncManager::Reset() {
  if (m_waitingForPeers && m_currentScenePtr) {
    GameUtils::SetScenePaused(m_currentScenePtr, false, false);
  }
  m_waitingForPeers = false;
  m_currentSceneName.clear();
  m_currentScenePtr = nullptr;
  m_currentBarrierId = 0;
  m_peerReadyForScene.clear();
  m_readySignalPending = false;
}
