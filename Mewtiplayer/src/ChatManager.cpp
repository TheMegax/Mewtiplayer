#include "ChatManager.h"
#include "NetworkManager.h"
#include "InputGhost.h"
#include "ImGuiHook.h"
#include "GameUtils.h"
#include "imgui.h"
#include <windows.h>
#include <cstdio>
#include <algorithm>

static std::string GetCurrentTimestampStr() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char timeBuf[16];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", st.wHour, st.wMinute);
    return std::string(timeBuf);
}

void ChatManager::AddMessage(uint64_t senderSteamID, const std::string &senderName, const std::string &text) {
    std::lock_guard<std::mutex> lock(m_mutex);
    ChatMessageEntry entry;
    entry.senderSteamID = senderSteamID;
    entry.senderName = senderName;
    entry.text = text;
    entry.timestamp = GetCurrentTimestampStr();
    entry.isSystem = false;

    m_messages.push_back(entry);
    while (m_messages.size() > MAX_CHAT_MESSAGES) {
        m_messages.pop_front();
    }
    m_scrollToBottom = true;

    for (auto *fighter : GameUtils::GetFighters()) {
        if (fighter && fighter->persistentChar && NetworkManager::Get().GetCatOwner(fighter->persistentChar->sql_key) == senderSteamID) {
            ParaboxAPI::ShowCombatPopup(fighter, text.c_str());
            break;
        }
    }
}

void ChatManager::AddSystemMessage(const std::string &text) {
    std::lock_guard<std::mutex> lock(m_mutex);
    ChatMessageEntry entry;
    entry.senderSteamID = 0;
    entry.senderName = "System";
    entry.text = text;
    entry.timestamp = GetCurrentTimestampStr();
    entry.isSystem = true;

    m_messages.push_back(entry);
    while (m_messages.size() > MAX_CHAT_MESSAGES) {
        m_messages.pop_front();
    }
    m_scrollToBottom = true;
}

void ChatManager::Render() {
    const ImGuiIO &io = ImGui::GetIO();
    const HWND hWnd = ImGuiHook::GetHWND();

    int vpX = 0, vpY = 0, vpW = (int)io.DisplaySize.x, vpH = (int)io.DisplaySize.y;
    if (hWnd) {
        InputGhost::GetViewportInfo(hWnd, vpX, vpY, vpW, vpH);
    } else {
        constexpr float targetAspect = 16.0f / 9.0f;
        const float windowAspect = io.DisplaySize.x / io.DisplaySize.y;
        if (windowAspect > targetAspect) {
            vpW = (int)(io.DisplaySize.y * targetAspect);
            vpH = (int)io.DisplaySize.y;
            vpX = (int)((io.DisplaySize.x - vpW) * 0.5f);
            vpY = 0;
        } else {
            vpW = (int)io.DisplaySize.x;
            vpH = (int)(io.DisplaySize.x / targetAspect);
            vpX = 0;
            vpY = (int)((io.DisplaySize.y - vpH) * 0.5f);
        }
    }

    const float chatW = (float)std::min(340, (int)(vpW * 0.32f));
    const float chatH = (float)std::min(220, (int)(vpH * 0.28f));
    const float chatX = (float)vpX + 15.0f;
    const float chatY = (float)vpY + ((float)vpH * 0.70f) - chatH - 10.0f;

    ImGui::SetNextWindowPos(ImVec2(chatX, chatY), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(chatW, chatH), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(200.0f, 100.0f), ImVec2((float)vpW, (float)vpH));

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.10f, 0.70f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.30f, 0.30f, 0.35f, 0.60f));

    if (!m_isTyping && (ImGui::IsKeyPressed(ImGuiKey_T, false) || ImGui::IsKeyPressed(ImGuiKey_Enter, false))) {
        m_focusInputNextFrame = true;
    }

    if (ImGui::Begin("Chat", nullptr)) {
        const ImVec2 winPos = ImGui::GetWindowPos();
        const ImVec2 winSize = ImGui::GetWindowSize();
        const float minX = (float)vpX;
        const float minY = (float)vpY;
        const float maxX = (float)(vpX + vpW);
        const float maxY = (float)(vpY + vpH);

        const float clampedX = std::clamp(winPos.x, minX, std::max(minX, maxX - winSize.x));
        const float clampedY = std::clamp(winPos.y, minY, std::max(minY, maxY - winSize.y));

        if (clampedX != winPos.x || clampedY != winPos.y) {
            ImGui::SetWindowPos(ImVec2(clampedX, clampedY));
        }

        if (!ImGui::IsWindowCollapsed()) {
            ImGui::BeginChild("##chat_scroll", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                ImGui::PushTextWrapPos(0.0f);
                for (const auto &msg : m_messages) {
                    if (msg.isSystem) {
                        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.20f, 1.0f), "[%s] [System] %s", msg.timestamp.c_str(), msg.text.c_str());
                    } else {
                        ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.85f, 1.0f), "[%s] %s: %s", msg.timestamp.c_str(), msg.senderName.c_str(), msg.text.c_str());
                    }
                }
                ImGui::PopTextWrapPos();

                if (m_scrollToBottom || ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                    ImGui::SetScrollHereY(1.0f);
                    m_scrollToBottom = false;
                }
            }
            ImGui::EndChild();

            if (m_focusInputNextFrame) {
                ImGui::SetKeyboardFocusHere();
                m_focusInputNextFrame = false;
            }

            ImGui::SetNextItemWidth(-1.0f);
            const bool submitted = ImGui::InputText("##chat_input_field", m_inputBuf, sizeof(m_inputBuf), ImGuiInputTextFlags_EnterReturnsTrue);
            m_isTyping = ImGui::IsItemActive() || ImGui::IsItemFocused();

            if (submitted) {
                if (m_inputBuf[0] != '\0') {
                    NetworkManager::Get().SendChatMessage(m_inputBuf);
                    m_inputBuf[0] = '\0';
                }
                ImGui::SetKeyboardFocusHere(-1);
                m_isTyping = false;
            }

            if (m_isTyping && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                ImGui::SetKeyboardFocusHere(-1);
                m_isTyping = false;
            }
        }
    }
    ImGui::End();

    ImGui::PopStyleColor(2);
}
