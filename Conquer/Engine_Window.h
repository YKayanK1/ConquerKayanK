
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

        // [Config.ini] Quando fullscreen=true, cria uma janela sem bordas (WS_POPUP) do tamanho
        // exato da tela primaria, imitando o modo "tela cheia sem janela" pedido no config.ini
        // (Fullscreen=1). Quando false, mantem o comportamento antigo de janela normal.
        bool Create(HINSTANCE hInstance, const wchar_t* title, bool fullscreen = false) {
            WNDCLASSEXW wcex = {};
            wcex.cbSize = sizeof(WNDCLASSEX);
            wcex.style = CS_HREDRAW | CS_VREDRAW;
            wcex.lpfnWndProc = StaticWindowProc;
            wcex.hInstance = hInstance;
            wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wcex.lpszClassName = L"KayanKConquerEngineClass";
            RegisterClassExW(&wcex);

            if (fullscreen) {
                m_width = GetSystemMetrics(SM_CXSCREEN);
                m_height = GetSystemMetrics(SM_CYSCREEN);
                m_hWnd = CreateWindowExW(0, L"KayanKConquerEngineClass", title, WS_POPUP | WS_VISIBLE,
                    0, 0, m_width, m_height, nullptr, nullptr, hInstance, this);
            }
            else {
                RECT rect = { 0, 0, m_width, m_height };
                AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
                int windowW = rect.right - rect.left;
                int windowH = rect.bottom - rect.top;
                m_hWnd = CreateWindowExW(0, L"KayanKConquerEngineClass", title, WS_OVERLAPPEDWINDOW,
                    CW_USEDEFAULT, CW_USEDEFAULT, windowW, windowH, nullptr, nullptr, hInstance, this);
            }

            if (!m_hWnd) return false;
            ShowWindow(m_hWnd, SW_SHOW);
            UpdateWindow(m_hWnd);
            return true;
        }
    };
}