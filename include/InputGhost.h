#pragma once
#include <cstdint>
#include <windows.h>

namespace InputGhost {
/**
 * @brief Normalize screen coordinates to 16:9 gameplay area (0.0 to 1.0).
 */
void NormalizeCoordinates(int x, int y, HWND hWnd, float &outX, float &outY);

/**
 * @brief Denormalize 16:9 gameplay area coordinates to screen coordinates.
 */
void DenormalizeCoordinates(float normX, float normY, HWND hWnd, int &outX,
                            int &outY);

/**
 * @brief Simulate a mouse click at normalized coordinates.
 */
void SimulateClick(UINT msg, float normX, float normY, HWND hWnd);

/**
 * @brief Simulate a key event.
 */
void SimulateKeyEvent(uint32_t type, uint32_t keycode, uint32_t scancode,
                      uint16_t mod, uint8_t down, uint8_t repeat);

/**
 * @brief Set whether the current instance is the host.
 */
void SetIsHost(bool host);

/**
 * @brief Check if the current thread is simulating an input (to prevent
 * recursion).
 */
bool IsSimulated();

/**
 * @brief Render debug info (e.g., a dot where the simulated mouse is).
 */
void RenderDebug();

/**
 * @brief Check if the system has observed a valid click for calibration.
 */
bool IsCalibrated();

/**
 * @brief Reset calibration data.
 */
void ResetCalibration();
} // namespace InputGhost
