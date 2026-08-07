// ============================================================================
// Conquer Kayank Engine
// ============================================================================
#pragma once
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include <tuple>
#include "../Graphics/Graphics.h"

namespace Game {
    static int GenerateColorDDS(Graphics::SceneRenderer& renderer, uint8_t r, uint8_t g, uint8_t b) {
        uint32_t dds[33] = { 0 };
        dds[0] = 0x20534444; dds[1] = 124; dds[2] = 0x100F; dds[3] = 1; dds[4] = 1; dds[5] = 4;
        dds[19] = 32; dds[20] = 0x41; dds[22] = 32;
        dds[23] = 0x00FF0000; dds[24] = 0x0000FF00; dds[25] = 0x000000FF; dds[26] = 0xFF000000;
        dds[27] = 0x1000;
        dds[32] = (0xFF << 24) | (r << 16) | (g << 8) | b;
        return renderer.LoadTextureFromMemory((const uint8_t*)dds, sizeof(dds));
    }

    
    static std::tuple<int, int, int> GenerateTextTexture(Graphics::SceneRenderer& renderer, const std::wstring& text, COLORREF textColor = RGB(255, 255, 255)) {
        HDC hdc = CreateCompatibleDC(NULL);

        HFONT hFont = CreateFontW(13, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Tahoma");
        SelectObject(hdc, hFont);

        RECT calcRect = { 0, 0, 0, 0 };
        DrawTextW(hdc, text.c_str(), -1, &calcRect, DT_CALCRECT);

        int width = calcRect.right - calcRect.left + 4;
        int height = calcRect.bottom - calcRect.top + 4;
        width = (width + 3) & ~3;
        height = (height + 3) & ~3;

        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = width;
        bmi.bmiHeader.biHeight = -height;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        HBITMAP hBmp = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
        SelectObject(hdc, hBmp);

        uint8_t* pPixels = static_cast<uint8_t*>(bits);
        for (int i = 0; i < width * height; ++i) {
            pPixels[i * 4 + 0] = 255;
            pPixels[i * 4 + 1] = 0;
            pPixels[i * 4 + 2] = 255;
            pPixels[i * 4 + 3] = 0;
        }

        SetBkMode(hdc, TRANSPARENT);

        SetTextColor(hdc, RGB(0, 0, 0));

        RECT r1 = { 1, 2, width, height }; DrawTextW(hdc, text.c_str(), -1, &r1, DT_LEFT | DT_TOP); // Esquerda
        RECT r2 = { 3, 2, width, height }; DrawTextW(hdc, text.c_str(), -1, &r2, DT_LEFT | DT_TOP); // Direita
        RECT r3 = { 2, 1, width, height }; DrawTextW(hdc, text.c_str(), -1, &r3, DT_LEFT | DT_TOP); // Cima
        RECT r4 = { 2, 3, width, height }; DrawTextW(hdc, text.c_str(), -1, &r4, DT_LEFT | DT_TOP); // Baixo

        SetTextColor(hdc, textColor);
        RECT rCore = { 2, 2, width, height };
        DrawTextW(hdc, text.c_str(), -1, &rCore, DT_LEFT | DT_TOP);

        for (int i = 0; i < width * height; ++i) {
            if (pPixels[i * 4 + 2] == 255 && pPixels[i * 4 + 1] == 0 && pPixels[i * 4 + 0] == 255) {
                pPixels[i * 4 + 0] = 0; pPixels[i * 4 + 1] = 0; pPixels[i * 4 + 2] = 0; pPixels[i * 4 + 3] = 0;
            }
            else {
                pPixels[i * 4 + 3] = 255;
            }
        }

        std::vector<uint8_t> ddsData(128 + width * height * 4, 0);
        uint32_t* header = reinterpret_cast<uint32_t*>(ddsData.data());
        header[0] = 0x20534444; header[1] = 124; header[2] = 0x100F;
        header[3] = height; header[4] = width; header[5] = width * 4;
        header[19] = 32; header[20] = 0x41; header[22] = 32;
        header[23] = 0x00FF0000; header[24] = 0x0000FF00; header[25] = 0x000000FF; header[26] = 0xFF000000;
        header[27] = 0x1000;
        std::memcpy(ddsData.data() + 128, bits, width * height * 4);

        DeleteObject(hBmp); DeleteObject(hFont); DeleteDC(hdc);

        int texId = renderer.LoadTextureFromMemory(ddsData.data(), ddsData.size());
        return { texId, width, height };
    }
}