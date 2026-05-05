#pragma once
#include <cstdint>
#include <windows.h>

namespace InputGhost {
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
 * @brief Simulate a mouse move at normalized coordinates.
 */
void SimulateMouseMove(float normX, float normY, HWND hWnd);

/**
 * @brief Set whether the current instance is the host.
 */
void SetIsHost(bool host);

/**
 * @brief Update the InputGhost state.
 */
void Update();

/**
 * @brief Register the CRC signature of a cursor type.
 */
void RegisterCursorSignature(uint32_t crc, uint8_t typeIndex);

/**
 * @brief Check if the current thread is simulating an input (to prevent
 * recursion).
 */
bool IsSimulated();

/**
 * @brief Check if the system has observed a valid click for calibration.
 */
bool IsCalibrated();

/**
 * @brief Reset calibration data.
 */
void ResetCalibration();
} // namespace InputGhost
