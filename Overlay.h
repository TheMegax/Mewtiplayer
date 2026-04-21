#pragma once
#include "mewjector.h"

namespace Overlay {
// Setup the Kiero-based ImGui hook.
void Setup(MewjectorAPI *mj);

// Append a line to the overlay log window.
void Log(const char *fmt, ...);
void LogV(const char *fmt, va_list args);

// Toggle overlay visibility.
void ToggleVisible();
} // namespace Overlay
