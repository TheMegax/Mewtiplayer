#include "Overlay.h"
#include "imgui.h"
#include "imgui_hook.h"
#include <deque>
#include <mutex>
#include <stdarg.h>
#include <string>
#include <windows.h>

static std::deque<std::string> g_logLines;
static std::mutex g_logMutex;
static const size_t MAX_LOG_LINES = 512;
static bool g_visible = true;

static MJ_fn_Log g_origMjLog = nullptr;

void Overlay::Log(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  LogV(fmt, args);
  va_end(args);
}

void Overlay::LogV(const char *fmt, va_list args) {
  char buf[1024];
  vsnprintf(buf, sizeof(buf), fmt, args);

  std::lock_guard<std::mutex> lock(g_logMutex);
  g_logLines.push_back(std::string(buf));
  while (g_logLines.size() > MAX_LOG_LINES)
    g_logLines.pop_front();
}

static void __cdecl WrappedMjLog(const char *owner, const char *fmt, ...) {
  char buf[2048];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  if (owner && strcmp(owner, "Multigenics") != 0) {
    Overlay::Log("[%s] %s", owner, buf);
  } else {
    Overlay::Log("%s", buf);
  }
}

void Overlay::ToggleVisible() { g_visible = !g_visible; }

static void InternalRender() {
  if (!g_visible)
    return;

  ImGui::SetNextWindowSize(ImVec2(680, 320), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
  ImGui::Begin("Multigenics  |  F1 to hide", &g_visible,
               ImGuiWindowFlags_NoNav);

  ImGui::BeginChild("##log", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()),
                    false, ImGuiWindowFlags_HorizontalScrollbar);
  {
    std::lock_guard<std::mutex> lock(g_logMutex);
    for (const auto &line : g_logLines) {
      ImVec4 col = ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
      if (line.find("[WARN]") != std::string::npos)
        col = ImVec4(1.0f, 0.85f, 0.2f, 1.0f);
      else if (line.find("[ERR]") != std::string::npos)
        col = ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
      else if (line.find("[OK]") != std::string::npos)
        col = ImVec4(0.4f, 1.0f, 0.5f, 1.0f);
      ImGui::TextColored(col, "%s", line.c_str());
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
      ImGui::SetScrollHereY(1.0f);
  }
  ImGui::EndChild();

  ImGui::Separator();
  ImGui::TextDisabled("F5 Host  |  F6 Join  |  F7 Ping  |  F1 Hide");
  ImGui::End();
}

void Overlay::Setup(MewjectorAPI *mj) {
  if (mj && mj->Log && mj->Log != WrappedMjLog) {
    g_origMjLog = mj->Log;
    mj->Log = WrappedMjLog;
  }

  if (!ImGuiHook::Load(mj, InternalRender, []() {
        Overlay::Log("[OK] Overlay active. F1=Menu");
      })) {
    mj->Log("Overlay", "[ERR] Failed to load ImGuiHook: %s",
            ImGuiHook::GetLastError().c_str());
  } else {
    mj->Log("Overlay", "[OK] Overlay initialized using Kiero.");
  }
}
