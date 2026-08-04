#pragma path
#pragma once
#include <string>
#include <deque>
#include <mutex>
#include <cstdint>

struct ChatMessageEntry {
    uint64_t senderSteamID;
    std::string senderName;
    std::string text;
    std::string timestamp;
    bool isSystem;
};

class ChatManager {
public:
    static ChatManager &Get() {
        static ChatManager instance;
        return instance;
    }

    void AddMessage(uint64_t senderSteamID, const std::string &senderName, const std::string &text);
    void AddSystemMessage(const std::string &text);
    void Render();

    [[nodiscard]] bool IsTyping() const { return m_isTyping; }

private:
    ChatManager() = default;

    std::deque<ChatMessageEntry> m_messages;
    std::mutex m_mutex;
    static constexpr size_t MAX_CHAT_MESSAGES = 200;

    char m_inputBuf[256] = "";
    bool m_focusInputNextFrame = false;
    bool m_isTyping = false;
    bool m_scrollToBottom = false;
};
