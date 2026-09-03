// ============================================================================
// Conquer Kayank Engine
// ============================================================================
#pragma once
#include <d3d11.h>
#include <dxgi.h>
#include <wrl.h>

namespace Graphics {

    class D3DContext {
    public:
        static D3DContext& GetInstance() { static D3DContext instance; return instance; }

        Microsoft::WRL::ComPtr<ID3D11Device> device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
        Microsoft::WRL::ComPtr<IDXGISwapChain> swapChain;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTargetView;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depthStencilView;

        int screenWidth, screenHeight;
        bool m_vsync = true;

        void Initialize(HWND hwnd, int width, int height) {
            screenWidth = width; screenHeight = height;

            DXGI_SWAP_CHAIN_DESC scd = {};
            scd.BufferCount = 1;
            scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            scd.BufferDesc.Width = width;
            scd.BufferDesc.Height = height;
            scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            scd.OutputWindow = hwnd;
            scd.SampleDesc.Count = 1;
            scd.Windowed = TRUE;

            D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                D3D11_SDK_VERSION, &scd, &swapChain, &device, nullptr, &context);

            ID3D11Texture2D* backBuffer = nullptr;
            swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&backBuffer);
            device->CreateRenderTargetView(backBuffer, nullptr, &renderTargetView);
            backBuffer->Release();

            D3D11_TEXTURE2D_DESC depthDesc = {};
            depthDesc.Width = width; depthDesc.Height = height;
            depthDesc.MipLevels = 1; depthDesc.ArraySize = 1;
            depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
            depthDesc.SampleDesc.Count = 1; depthDesc.Usage = D3D11_USAGE_DEFAULT;
            depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

            ID3D11Texture2D* depthTex = nullptr;
            device->CreateTexture2D(&depthDesc, nullptr, &depthTex);
            device->CreateDepthStencilView(depthTex, nullptr, &depthStencilView);
            depthTex->Release();

            context->OMSetRenderTargets(1, renderTargetView.GetAddressOf(), depthStencilView.Get());

            D3D11_VIEWPORT viewport = {};
            viewport.TopLeftX = 0; viewport.TopLeftY = 0;
            viewport.Width = (float)width; viewport.Height = (float)height;
            viewport.MinDepth = 0.0f; viewport.MaxDepth = 1.0f;
            context->RSSetViewports(1, &viewport);
        }

        void BeginFrame(float r, float g, float b) {
            float color[4] = { r, g, b, 1.0f };
            context->ClearRenderTargetView(renderTargetView.Get(), color);
            context->ClearDepthStencilView(depthStencilView.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
        }

        void EndFrame() { swapChain->Present(m_vsync ? 1 : 0, 0); }

        void Resize(int width, int height) {
            if (!device || width == 0 || height == 0) return;
            screenWidth = width; screenHeight = height;

            context->OMSetRenderTargets(0, nullptr, nullptr);
            renderTargetView.Reset();
            depthStencilView.Reset();

            swapChain->ResizeBuffers(1, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0);

            ID3D11Texture2D* backBuffer = nullptr;
            swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&backBuffer);
            device->CreateRenderTargetView(backBuffer, nullptr, &renderTargetView);
            backBuffer->Release();

            D3D11_TEXTURE2D_DESC depthDesc = {};
            depthDesc.Width = width; depthDesc.Height = height;
            depthDesc.MipLevels = 1; depthDesc.ArraySize = 1;
            depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
            depthDesc.SampleDesc.Count = 1; depthDesc.Usage = D3D11_USAGE_DEFAULT;
            depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

            ID3D11Texture2D* depthTex = nullptr;
            device->CreateTexture2D(&depthDesc, nullptr, &depthTex);
            device->CreateDepthStencilView(depthTex, nullptr, &depthStencilView);
            depthTex->Release();

            context->OMSetRenderTargets(1, renderTargetView.GetAddressOf(), depthStencilView.Get());

            D3D11_VIEWPORT viewport = {};
            viewport.TopLeftX = 0; viewport.TopLeftY = 0;
            viewport.Width = (float)width; viewport.Height = (float)height;
            viewport.MinDepth = 0.0f; viewport.MaxDepth = 1.0f;
            context->RSSetViewports(1, &viewport);
        }
    };

}