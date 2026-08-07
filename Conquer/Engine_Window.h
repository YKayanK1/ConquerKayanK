
// ============================================================================
// Conquer Kayank Engine
// ============================================================================
#pragma once
#define NOMINMAX
#include <windows.h>
#include <functional>

namespace Engine {
    class WindowManager {
    public:
        HWND m_hWnd = nullptr;
        int m_width = 1280;
        int m_height = 720;

        std::function<void(int)> onMouseWheel;

        static LRESULT CALLBACK StaticWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
            WindowManager* pThis = nullptr;
            if (message == WM_NCCREATE) {
                CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
                pThis = (WindowManager*)pCreate->lpCreateParams;
                SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pThis);
            }
            else {
                pThis = (WindowManager*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
            }
            if (pThis) return pThis->WindowProc(hWnd, message, wParam, lParam);
            return DefWindowProc(hWnd, message, wParam, lParam);
        }

        LRESULT WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
            switch (message) {
            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
            case WM_MOUSEWHEEL:
                if (onMouseWheel) onMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
                return 0;
            }
            return DefWindowProc(hWnd, message, wParam, lParam);
        }

        bool Create(HINSTANCE hInstance, const wchar_t* title) {
            WNDCLASSEXW wcex = {};
            wcex.cbSize = sizeof(WNDCLASSEX);
            wcex.style = CS_HREDRAW | CS_VREDRAW;
            wcex.lpfnWndProc = StaticWindowProc;
            wcex.hInstance = hInstance;
            wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wcex.lpszClassName = L"KayanKConquerEngineClass";
            RegisterClassExW(&wcex);

            m_hWnd = CreateWindowExW(0, L"KayanKConquerEngineClass", title, WS_OVERLAPPEDWINDOW,
                CW_USEDEFAULT, CW_USEDEFAULT, m_width, m_height, nullptr, nullptr, hInstance, this);

            if (!m_hWnd) return false;
            ShowWindow(m_hWnd, SW_SHOW);
            UpdateWindow(m_hWnd);
            return true;
        }
    };
}