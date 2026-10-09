#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <windows.h>
#include "NetworkManager.h"
#include "ParaboxAPI.h"
#include "MewgenicsTypes.h"

class SceneSyncManager {
public:
  static SceneSyncManager &Get() {
    static SceneSyncManager instance;
    return instance;
  }

  void Init();
  void Update();

  void OnSceneAdded(Scene *scene, const char *sceneName);
  void OnResumeMap(Scene *mapScene);

  void HandleSceneLoadReady(uint64_t senderSteamID, const SceneLoadReadyPacket *pkt);
  void HandleSceneLoadProceed(const SceneLoadProceedPacket *pkt);

  void ForceProceed();

  [[nodiscard]] bool IsWaitingForPeers() const { return m_waitingForPeers; }
  [[nodiscard]] const std::string &GetCurrentSceneName() const { return m_currentSceneName; }
  [[nodiscard]] uint32_t GetCurrentBarrierId() const { return m_currentBarrierId; }

  struct PeerStatus {
    uint64_t steamID;
    std::string name;
    bool isReady;
  };
  [[nodiscard]] std::vector<PeerStatus> GetPeerStatuses() const;

  void Reset();

private:
  SceneSyncManager() = default;

  void StartBarrier(Scene *scene, const std::string &sceneName);
  void SendReadySignal();
  void CheckAllPeersReady();
  void ProceedBarrier();

  bool m_waitingForPeers = false;
  std::string m_currentSceneName;
  Scene *m_currentScenePtr = nullptr;
  uint32_t m_currentBarrierId = 0;

  // steamID -> sceneName
  std::map<uint64_t, std::string> m_peerReadyForScene;

  bool m_readySignalPending = false;
  ULONGLONG m_readySignalSendTime = 0;

  ULONGLONG m_barrierStartTime = 0;
  static constexpr ULONGLONG BARRIER_TIMEOUT_MS = 25000;
};
