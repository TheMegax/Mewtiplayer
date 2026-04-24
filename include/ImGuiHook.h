#pragma once

#include "mewjector.h"
#include <functional>
#include <string>
#include <windows.h>

namespace ImGuiHook {
/**
 * @brief Load the ImGui hook.
 */
bool Load(
    MewjectorAPI *mj, const std::function<void()> &render,
    const std::function<void()> &init = []() {});

/**
 * @brief Unload the ImGui hook.
 */
void Unload();

/**
 * @brief Get the last error message.
 */
std::string GetLastError();
HWND GetHWND();
} // namespace ImGuiHook
