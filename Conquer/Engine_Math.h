
// ============================================================================
// Conquer Kayank Engine
// ============================================================================
#pragma once
#include <utility>

namespace Engine {
    class IsometricCoordinateSystem {
        int puzzlePixelWidth, puzzlePixelHeight, mapHeight;
    public:
        IsometricCoordinateSystem(int pWidth, int pHeight, int mHeight)
            : puzzlePixelWidth(pWidth), puzzlePixelHeight(pHeight), mapHeight(mHeight) {
        }

        std::pair<float, float> MapToScreen(float mapX, float mapY) {
            float x = (mapX - mapY) * 32.0f + (puzzlePixelWidth / 2.0f);
            float y = (mapX + mapY - (mapHeight - 1)) * 16.0f + (puzzlePixelHeight / 2.0f);
            return { x, y };
        }

        std::pair<float, float> ScreenToMap(float screenX, float screenY, float cameraX, float cameraY) {
            float worldX = screenX + cameraX;
            float worldY = screenY + cameraY;
            float a = (worldX - (puzzlePixelWidth / 2.0f)) / 32.0f;
            float b = (worldY - (puzzlePixelHeight / 2.0f)) / 16.0f + (mapHeight - 1);
            float mapX = (a + b) / 2.0f;
            float mapY = (b - a) / 2.0f;
            return { mapX, mapY };
        }
    };
}