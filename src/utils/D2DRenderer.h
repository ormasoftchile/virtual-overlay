#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <wrl/client.h>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

namespace VirtualOverlay {

using Microsoft::WRL::ComPtr;

// Direct2D/DirectWrite renderer for overlay
class D2DRenderer {
public:
    static D2DRenderer& Instance();

    // Initialize Direct2D and DirectWrite factories
    bool Init();
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    // Create render target for a window
    bool CreateRenderTarget(HWND hwnd, ID2D1HwndRenderTarget** ppRenderTarget);

    // Create text format
    bool CreateTextFormat(
        const std::wstring& fontFamily,
        float fontSize,
        DWRITE_FONT_WEIGHT fontWeight,
        IDWriteTextFormat** ppTextFormat
    );

    // Create text layout for measuring/rendering
    bool CreateTextLayout(
        const std::wstring& text,
        IDWriteTextFormat* pTextFormat,
        float maxWidth,
        float maxHeight,
        IDWriteTextLayout** ppTextLayout
    );

    // Get factories
    ID2D1Factory* GetD2DFactory() const { return m_d2dFactory.Get(); }
    IDWriteFactory* GetDWriteFactory() const { return m_dwriteFactory.Get(); }

    // Helper: Convert RGB to D2D color
    static D2D1_COLOR_F ColorFromRGB(uint32_t rgb, float alpha = 1.0f);

    // Helper: Create rounded rect geometry
    bool CreateRoundedRectGeometry(
        const D2D1_RECT_F& rect,
        float radiusX,
        float radiusY,
        ID2D1RoundedRectangleGeometry** ppGeometry
    );

private:
    D2DRenderer() = default;
    ~D2DRenderer();
    D2DRenderer(const D2DRenderer&) = delete;
    D2DRenderer& operator=(const D2DRenderer&) = delete;

    ComPtr<ID2D1Factory> m_d2dFactory;
    ComPtr<IDWriteFactory> m_dwriteFactory;
    bool m_initialized = false;
};

}  // namespace VirtualOverlay
