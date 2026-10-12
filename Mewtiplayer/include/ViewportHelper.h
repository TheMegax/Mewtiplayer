#pragma once
#include <cstdint>
#include <windows.h>

namespace ViewportHelper {
/**
 * @brief Get the viewport information for the 16:9 gameplay area.
 */
void GetViewportInfo(HWND hWnd, int &vpX, int &vpY, int &vpW, int &vpH);

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
 * @brief Update the mouse and viewport state.
 */
void Update();
} // namespace ViewportHelper
