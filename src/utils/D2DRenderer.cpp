#include "D2DRenderer.h"
#include "Logger.h"

namespace VirtualOverlay {

D2DRenderer& D2DRenderer::Instance() {
    static D2DRenderer instance;
    return instance;
}

D2DRenderer::~D2DRenderer() {
    if (m_initialized) {
        Shutdown();
    }
}

bool D2DRenderer::Init() {
    if (m_initialized) {
        return true;
    }

    // Create Direct2D factory
    HRESULT hr = D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED,
        m_d2dFactory.GetAddressOf()
    );

    if (FAILED(hr)) {
        LOG_ERROR("Failed to create D2D factory: 0x%08X", hr);
        return false;
    }

    // Create DirectWrite factory
    hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(m_dwriteFactory.GetAddressOf())
    );

    if (FAILED(hr)) {
        LOG_ERROR("Failed to create DWrite factory: 0x%08X", hr);
        m_d2dFactory.Reset();
        return false;
    }

    m_initialized = true;
    LOG_INFO("D2DRenderer initialized");
    return true;
}

void D2DRenderer::Shutdown() {
    if (!m_initialized) {
        return;
    }

    m_dwriteFactory.Reset();
    m_d2dFactory.Reset();
    m_initialized = false;
    LOG_INFO("D2DRenderer shutdown");
}

bool D2DRenderer::CreateRenderTarget(HWND hwnd, ID2D1HwndRenderTarget** ppRenderTarget) {
    if (!m_initialized || !hwnd || !ppRenderTarget) {
        return false;
    }

    RECT rc;
    GetClientRect(hwnd, &rc);

    D2D1_SIZE_U size = D2D1::SizeU(
        rc.right - rc.left,
        rc.bottom - rc.top
    );

    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );

    D2D1_HWND_RENDER_TARGET_PROPERTIES hwndRtProps = D2D1::HwndRenderTargetProperties(
        hwnd,
        size,
        D2D1_PRESENT_OPTIONS_IMMEDIATELY
    );

    HRESULT hr = m_d2dFactory->CreateHwndRenderTarget(
        rtProps,
        hwndRtProps,
        ppRenderTarget
    );

    if (FAILED(hr)) {
        LOG_ERROR("Failed to create render target: 0x%08X", hr);
        return false;
    }

    return true;
}

bool D2DRenderer::CreateTextFormat(
    const std::wstring& fontFamily,
    float fontSize,
    DWRITE_FONT_WEIGHT fontWeight,
    IDWriteTextFormat** ppTextFormat
) {
    if (!m_initialized || !ppTextFormat) {
        return false;
    }

    HRESULT hr = m_dwriteFactory->CreateTextFormat(
        fontFamily.c_str(),
        nullptr,  // Font collection (nullptr = system)
        fontWeight,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        fontSize,
        L"en-US",
        ppTextFormat
    );

    if (FAILED(hr)) {
        LOG_ERROR("Failed to create text format: 0x%08X", hr);
        return false;
    }

    // Center alignment
    (*ppTextFormat)->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    (*ppTextFormat)->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    return true;
}

bool D2DRenderer::CreateTextLayout(
    const std::wstring& text,
    IDWriteTextFormat* pTextFormat,
    float maxWidth,
    float maxHeight,
    IDWriteTextLayout** ppTextLayout
) {
    if (!m_initialized || !pTextFormat || !ppTextLayout) {
        return false;
    }

    HRESULT hr = m_dwriteFactory->CreateTextLayout(
        text.c_str(),
        static_cast<UINT32>(text.length()),
        pTextFormat,
        maxWidth,
        maxHeight,
        ppTextLayout
    );

    if (FAILED(hr)) {
        LOG_ERROR("Failed to create text layout: 0x%08X", hr);
        return false;
    }

    return true;
}

D2D1_COLOR_F D2DRenderer::ColorFromRGB(uint32_t rgb, float alpha) {
    return D2D1::ColorF(
        static_cast<float>((rgb >> 16) & 0xFF) / 255.0f,  // R
        static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,   // G
        static_cast<float>(rgb & 0xFF) / 255.0f,          // B
        alpha
    );
}

bool D2DRenderer::CreateRoundedRectGeometry(
    const D2D1_RECT_F& rect,
    float radiusX,
    float radiusY,
    ID2D1RoundedRectangleGeometry** ppGeometry
) {
    if (!m_initialized || !ppGeometry) {
        return false;
    }

    D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(rect, radiusX, radiusY);

    HRESULT hr = m_d2dFactory->CreateRoundedRectangleGeometry(
        roundedRect,
        ppGeometry
    );

    if (FAILED(hr)) {
        LOG_ERROR("Failed to create rounded rect geometry: 0x%08X", hr);
        return false;
    }

    return true;
}

}  // namespace VirtualOverlay
