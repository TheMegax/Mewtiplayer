#pragma once
#include "mewjector.h"
#include <cstdint>

namespace Overlay {
// Setup the Kiero-based ImGui hook.
void Setup(MewjectorAPI *mj);

// Append a line to the overlay log window.
void Log(const char *fmt, ...);
void LogV(const char *fmt, va_list args);

// Toggle overlay visibility.
void ToggleVisible();
// Update remote cursor info.
void UpdateRemoteCursor(uint64_t steamID, float x, float y, uint8_t type);

// Deferred save/load.
void RequestSaveLoad();
bool ConsumeSaveLoad();
} // namespace Overlay
