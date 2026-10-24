#include "MirrorWindow.h"
#include "../utils/Logger.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <d2d1_1.h>
#include <magnification.h>
#include <mutex>
#include <atomic>
#include <thread>
#include <condition_variable>
#include <chrono>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <wincodec.h>

#pragma comment(lib, "magnification.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "windowsapp.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace winrt_cap = winrt::Windows::Graphics::Capture;
namespace winrt_d3d = winrt::Windows::Graphics::DirectX::Direct3D11;

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

namespace VirtualOverlay {

constexpr wchar_t MirrorWindow::WINDOW_CLASS[];

struct MirrorWindow::Impl {
    bool useWgc = false;

    // WGC / D3D11 / D2D resources
    ID3D11Device* d3dDevice = nullptr;
    ID3D11DeviceContext* d3dContext = nullptr;
    IDXGISwapChain1* swapChain = nullptr;
    ID2D1Factory1* d2dFactory = nullptr;
    ID2D1Device* d2dDevice = nullptr;
    ID2D1DeviceContext* d2dContext = nullptr;
    ID2D1Bitmap1* targetBitmap = nullptr;

    winrt_cap::GraphicsCaptureItem captureItem{ nullptr };
    winrt_cap::Direct3D11CaptureFramePool framePool{ nullptr };
    winrt_cap::GraphicsCaptureSession captureSession{ nullptr };
    winrt::event_token frameArrivedToken{};

    std::mutex renderMutex;
    std::atomic<bool> running{ false };
    D2D1_MATRIX_3X2_F flipMatrix = {};

    // Owned copy of the latest captured frame. Keeping our own copy lets us
    // re-render (to move the surrogate cursor) even when no new capture frame
    // has arrived, e.g. while the desktop is static.
    ID2D1Bitmap1* contentBitmap = nullptr;
    D2D1_SIZE_U contentSize = {};

    // Surrogate (mirrored) cursor drawn into the flipped content. The real
    // system cursor is hidden over the mirror; this gives the user correct
    // visual feedback so clicks/hover/drag land on the content they see.
    std::atomic<bool> drawCursor{ false };
    IWICImagingFactory* wicFactory = nullptr;
    HCURSOR cachedCursor = nullptr;
    ID2D1Bitmap* cursorBitmap = nullptr;
    POINT cursorHotspot = {};
    D2D1_SIZE_F cursorSize = {};

    // Monitor origin/size in desktop pixels (for cursor placement).
    LONG originLeft = 0;
    LONG originTop = 0;
    UINT widthPx = 0;
    UINT heightPx = 0;

    // Dedicated render thread: presenting (vsync-blocking, possibly
    // cross-adapter) must never run on the UI thread.
    std::thread renderThread;
    std::mutex wakeMutex;
    std::condition_variable wakeCv;
    bool frameDirty = false;
    bool stopRender = false;

    // Fallback Magnifier child window
    HWND magHwnd = nullptr;
    static constexpr UINT_PTR TIMER_REFRESH = 1001;
    static constexpr UINT TIMER_REFRESH_MS = 16;  // ~60 FPS
    static constexpr DWORD CURSOR_POLL_MS = 4;

    bool EnsureWic() {
        if (wicFactory) {
            return true;
        }
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                      CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wicFactory));
        return SUCCEEDED(hr) && wicFactory != nullptr;
    }

    // Build a Direct2D bitmap of the given cursor shape (color + mask composited
    // to premultiplied BGRA) so it can be drawn into the flipped overlay.
    void UpdateCursorBitmap(HCURSOR hCursor) {
        if (cursorBitmap) { cursorBitmap->Release(); cursorBitmap = nullptr; }
        cachedCursor = hCursor;
        cursorSize = {};
        cursorHotspot = {};
        if (!hCursor || !d2dContext || !EnsureWic()) {
            return;
        }

        IWICBitmap* wicBmp = nullptr;
        if (FAILED(wicFactory->CreateBitmapFromHICON(hCursor, &wicBmp)) || !wicBmp) {
            return;
        }

        UINT w = 0, h = 0;
        wicBmp->GetSize(&w, &h);
        cursorSize = D2D1::SizeF(static_cast<float>(w), static_cast<float>(h));

        IWICFormatConverter* conv = nullptr;
        if (SUCCEEDED(wicFactory->CreateFormatConverter(&conv)) && conv) {
            if (SUCCEEDED(conv->Initialize(wicBmp, GUID_WICPixelFormat32bppPBGRA,
                                           WICBitmapDitherTypeNone, nullptr, 0.0,
                                           WICBitmapPaletteTypeCustom))) {
                d2dContext->CreateBitmapFromWicBitmap(conv, nullptr, &cursorBitmap);
            }
            conv->Release();
        }
        wicBmp->Release();

        ICONINFO ii = {};
        if (GetIconInfo(hCursor, &ii)) {
            cursorHotspot.x = static_cast<LONG>(ii.xHotspot);
            cursorHotspot.y = static_cast<LONG>(ii.yHotspot);
            if (ii.hbmColor) DeleteObject(ii.hbmColor);
            if (ii.hbmMask) DeleteObject(ii.hbmMask);
        }
    }

    // Draw the cached content flipped, plus the surrogate cursor, and present.
    void Render() {
        std::lock_guard<std::mutex> lock(renderMutex);
        if (!running || !d2dContext || !swapChain || !contentBitmap) {
            return;
        }

        d2dContext->BeginDraw();
        d2dContext->Clear(D2D1::ColorF(D2D1::ColorF::Black));
        d2dContext->SetTransform(flipMatrix);
        d2dContext->DrawBitmap(contentBitmap);

        if (drawCursor) {
            CURSORINFO ci = { sizeof(ci) };
            if (GetCursorInfo(&ci) && (ci.flags & CURSOR_SHOWING) && ci.hCursor) {
                if (ci.hCursor != cachedCursor) {
                    UpdateCursorBitmap(ci.hCursor);
                }
                if (cursorBitmap) {
                    const float lx = static_cast<float>(ci.ptScreenPos.x - originLeft - cursorHotspot.x);
                    const float ly = static_cast<float>(ci.ptScreenPos.y - originTop - cursorHotspot.y);
                    // Draw through the same flip transform so the cursor is
                    // mirrored and positioned exactly like the captured content.
                    d2dContext->DrawBitmap(
                        cursorBitmap,
                        D2D1::RectF(lx, ly, lx + cursorSize.width, ly + cursorSize.height));
                }
            }
        }

        if (SUCCEEDED(d2dContext->EndDraw())) {
            swapChain->Present(1, 0);
        }
    }

    void StartRenderThread() {
        stopRender = false;
        frameDirty = false;
        renderThread = std::thread([this] { RenderLoop(); });
    }

    void StopRenderThread() {
        {
            std::lock_guard<std::mutex> lk(wakeMutex);
            stopRender = true;
        }
        wakeCv.notify_all();
        if (renderThread.joinable()) {
            renderThread.join();
        }
    }

    void SignalFrame() {
        {
            std::lock_guard<std::mutex> lk(wakeMutex);
            frameDirty = true;
        }
        wakeCv.notify_one();
    }

    void RenderLoop() {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

        POINT lastPos = { LONG_MIN, LONG_MIN };
        HCURSOR lastShape = nullptr;
        for (;;) {
            bool dirty = false;
            {
                std::unique_lock<std::mutex> lk(wakeMutex);
                wakeCv.wait_for(lk, std::chrono::milliseconds(CURSOR_POLL_MS),
                                [this] { return frameDirty || stopRender; });
                if (stopRender) break;
                dirty = frameDirty;
                frameDirty = false;
            }

            if (drawCursor) {
                CURSORINFO ci = { sizeof(ci) };
                if (GetCursorInfo(&ci) &&
                    (ci.ptScreenPos.x != lastPos.x || ci.ptScreenPos.y != lastPos.y ||
                     ci.hCursor != lastShape)) {
                    lastPos = ci.ptScreenPos;
                    lastShape = ci.hCursor;
                    dirty = true;
                }
            }

            if (dirty) {
                Render();
            }
        }

        CoUninitialize();
    }

    void Cleanup(HWND hostHwnd) {
        running = false;
        StopRenderThread();

        // Tear down capture WITHOUT holding renderMutex: closing waits for any
        // in-flight FrameArrived callback, which itself takes renderMutex.
        if (framePool && frameArrivedToken.value != 0) {
            try { framePool.FrameArrived(frameArrivedToken); } catch (...) {}
            frameArrivedToken = {};
        }
        if (captureSession) {
            try { captureSession.Close(); } catch (...) {}
            captureSession = nullptr;
        }
        if (framePool) {
            try { framePool.Close(); } catch (...) {}
            framePool = nullptr;
        }
        captureItem = nullptr;

        std::lock_guard<std::mutex> lock(renderMutex);

        if (cursorBitmap) { cursorBitmap->Release(); cursorBitmap = nullptr; }
        cachedCursor = nullptr;
        if (wicFactory) { wicFactory->Release(); wicFactory = nullptr; }
        if (contentBitmap) { contentBitmap->Release(); contentBitmap = nullptr; }
        contentSize = {};
        if (targetBitmap) { targetBitmap->Release(); targetBitmap = nullptr; }
        if (d2dContext) { d2dContext->Release(); d2dContext = nullptr; }
        if (d2dDevice) { d2dDevice->Release(); d2dDevice = nullptr; }
        if (d2dFactory) { d2dFactory->Release(); d2dFactory = nullptr; }
        if (swapChain) { swapChain->Release(); swapChain = nullptr; }
        if (d3dContext) { d3dContext->Release(); d3dContext = nullptr; }
        if (d3dDevice) { d3dDevice->Release(); d3dDevice = nullptr; }

        if (magHwnd) {
            if (hostHwnd) {
                KillTimer(hostHwnd, TIMER_REFRESH);
            }
            DestroyWindow(magHwnd);
            magHwnd = nullptr;
        }
        useWgc = false;
    }
};

MirrorWindow::MirrorWindow()
    : m_impl(std::make_unique<Impl>()) {
}

MirrorWindow::~MirrorWindow() {
    Destroy();
}

void MirrorWindow::EnsureClassRegistered(HINSTANCE hInstance) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = WINDOW_CLASS;

    if (!RegisterClassExW(&wc)) {
        DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            LOG_WARN("MirrorWindow: failed to register window class: %lu", error);
        }
    }
}

bool MirrorWindow::Create(HINSTANCE hInstance, const std::wstring& deviceName, const RECT& bounds, HMONITOR hMonitor) {
    if (m_hwnd) {
        Destroy();
    }

    m_hInstance = hInstance;
    m_deviceName = deviceName;
    m_bounds = bounds;
    m_hMonitor = hMonitor;

    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    if (width <= 0 || height <= 0) {
        LOG_WARN("MirrorWindow: invalid bounds for %ws", deviceName.c_str());
        return false;
    }

    m_impl->originLeft = bounds.left;
    m_impl->originTop = bounds.top;
    m_impl->widthPx = static_cast<UINT>(width);
    m_impl->heightPx = static_cast<UINT>(height);
    m_impl->drawCursor = m_magnifiedCursor;

    EnsureClassRegistered(hInstance);

    // Click-through, topmost, non-activating tool window.
    // WS_EX_LAYERED is required: WS_EX_TRANSPARENT / HTTRANSPARENT alone only
    // pass input to windows of this same thread, so other applications' windows
    // would never receive clicks, hover or drags. LAYERED + TRANSPARENT makes
    // the system hit-test skip this window entirely.
    const DWORD exStyle = WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW |
                          WS_EX_TRANSPARENT | WS_EX_LAYERED;
    const DWORD style = WS_POPUP | WS_CLIPCHILDREN;

    m_hwnd = CreateWindowExW(
        exStyle, WINDOW_CLASS, L"", style,
        bounds.left, bounds.top, width, height,
        nullptr, nullptr, hInstance, this);
    if (!m_hwnd) {
        LOG_ERROR("MirrorWindow: failed to create host window: %lu", GetLastError());
        return false;
    }
    SetLayeredWindowAttributes(m_hwnd, 0, 255, LWA_ALPHA);

    // Tell DWM to exclude this overlay window from screen captures so it never mirrors itself.
    SetWindowDisplayAffinity(m_hwnd, WDA_EXCLUDEFROMCAPTURE);

    // Try Windows.Graphics.Capture first
    bool wgcOk = false;
    try {
        if (hMonitor && winrt_cap::GraphicsCaptureSession::IsSupported()) {
            // 1. Create D3D11 device
            D3D_FEATURE_LEVEL fl;
            HRESULT hr = D3D11CreateDevice(
                nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                nullptr, 0, D3D11_SDK_VERSION,
                &m_impl->d3dDevice, &fl, &m_impl->d3dContext);

            if (SUCCEEDED(hr)) {
                IDXGIDevice* dxgiDevice = nullptr;
                m_impl->d3dDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);

                IDXGIAdapter* dxgiAdapter = nullptr;
                dxgiDevice->GetAdapter(&dxgiAdapter);

                IDXGIFactory2* dxgiFactory = nullptr;
                dxgiAdapter->GetParent(__uuidof(IDXGIFactory2), (void**)&dxgiFactory);

                // 2. Create SwapChain
                DXGI_SWAP_CHAIN_DESC1 scDesc = {};
                scDesc.Width = width;
                scDesc.Height = height;
                scDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
                scDesc.SampleDesc.Count = 1;
                scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                scDesc.BufferCount = 2;
                scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

                hr = dxgiFactory->CreateSwapChainForHwnd(
                    m_impl->d3dDevice, m_hwnd, &scDesc, nullptr, nullptr, &m_impl->swapChain);

                if (SUCCEEDED(hr)) {
                    // 3. Create Direct2D Context
                    D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, &m_impl->d2dFactory);
                    m_impl->d2dFactory->CreateDevice(dxgiDevice, &m_impl->d2dDevice);
                    m_impl->d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &m_impl->d2dContext);

                    IDXGISurface* backBuffer = nullptr;
                    m_impl->swapChain->GetBuffer(0, __uuidof(IDXGISurface), (void**)&backBuffer);
                    m_impl->d2dContext->CreateBitmapFromDxgiSurface(backBuffer, nullptr, &m_impl->targetBitmap);
                    m_impl->d2dContext->SetTarget(m_impl->targetBitmap);
                    backBuffer->Release();

                    // 4. Wrap D3D11 Device for WinRT
                    winrt::com_ptr<::IInspectable> inspectableDevice;
                    CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice, inspectableDevice.put());
                    auto winrtDevice = inspectableDevice.as<winrt_d3d::IDirect3DDevice>();

                    // 5. Create GraphicsCaptureItem from HMONITOR
                    auto interopFactory = winrt::get_activation_factory<winrt_cap::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
                    interopFactory->CreateForMonitor(
                        hMonitor,
                        winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(),
                        winrt::put_abi(m_impl->captureItem));

                    if (m_impl->captureItem) {
                        auto itemSize = m_impl->captureItem.Size();
                        m_impl->framePool = winrt_cap::Direct3D11CaptureFramePool::CreateFreeThreaded(
                            winrtDevice,
                            winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
                            2, itemSize);

                        m_impl->captureSession = m_impl->framePool.CreateCaptureSession(m_impl->captureItem);

                        try { m_impl->captureSession.IsCursorCaptureEnabled(false); } catch (...) {}
                        try { m_impl->captureSession.IsBorderRequired(false); } catch (...) {}

                        // Mirror horizontally around center of monitor
                        m_impl->flipMatrix = D2D1::Matrix3x2F::Scale(-1.0f, 1.0f, D2D1::Point2F(width / 2.0f, 0.0f));
                        m_impl->running = true;

                        m_impl->frameArrivedToken = m_impl->framePool.FrameArrived(
                            [this](winrt_cap::Direct3D11CaptureFramePool const& sender, winrt::Windows::Foundation::IInspectable const&) {
                                {
                                    std::lock_guard<std::mutex> lock(m_impl->renderMutex);
                                    if (!m_impl->running || !m_impl->d2dContext) return;

                                    auto frame = sender.TryGetNextFrame();
                                    if (!frame) return;

                                    auto surface = frame.Surface();
                                    auto surfaceInterop = surface.as<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
                                    IDXGISurface* capSurface = nullptr;
                                    if (SUCCEEDED(surfaceInterop->GetInterface(__uuidof(IDXGISurface), (void**)&capSurface)) && capSurface) {
                                        ID2D1Bitmap1* capBitmap = nullptr;
                                        if (SUCCEEDED(m_impl->d2dContext->CreateBitmapFromDxgiSurface(capSurface, nullptr, &capBitmap)) && capBitmap) {
                                            const D2D1_SIZE_U sz = capBitmap->GetPixelSize();
                                            if (!m_impl->contentBitmap ||
                                                m_impl->contentSize.width != sz.width ||
                                                m_impl->contentSize.height != sz.height) {
                                                if (m_impl->contentBitmap) { m_impl->contentBitmap->Release(); m_impl->contentBitmap = nullptr; }
                                                D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
                                                    D2D1_BITMAP_OPTIONS_NONE,
                                                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
                                                if (SUCCEEDED(m_impl->d2dContext->CreateBitmap(sz, nullptr, 0, props, &m_impl->contentBitmap))) {
                                                    m_impl->contentSize = sz;
                                                }
                                            }
                                            if (m_impl->contentBitmap) {
                                                D2D1_POINT_2U dst = { 0, 0 };
                                                D2D1_RECT_U src = { 0, 0, sz.width, sz.height };
                                                m_impl->contentBitmap->CopyFromBitmap(&dst, capBitmap, &src);
                                            }
                                            capBitmap->Release();
                                        }
                                        capSurface->Release();
                                    }
                                }
                                m_impl->SignalFrame();
                            });

                        m_impl->captureSession.StartCapture();
                        m_impl->useWgc = true;
                        wgcOk = true;
                        m_impl->StartRenderThread();

                        LOG_INFO("MirrorWindow: using Windows.Graphics.Capture for %ws (%dx%d)",
                                 deviceName.c_str(), width, height);
                    }
                }

                if (dxgiFactory) dxgiFactory->Release();
                if (dxgiAdapter) dxgiAdapter->Release();
                if (dxgiDevice) dxgiDevice->Release();
            }
        }
    } catch (const std::exception& e) {
        LOG_WARN("MirrorWindow: WGC init failed (%s), falling back to Magnification API", e.what());
    } catch (...) {
        LOG_WARN("MirrorWindow: WGC init threw unknown exception, falling back to Magnification API");
    }

    // Fallback to Magnification API if WGC setup was not successful
    if (!wgcOk) {
        m_impl->Cleanup(m_hwnd);

        DWORD magStyle = WS_CHILD | WS_VISIBLE;
        if (m_magnifiedCursor) {
            magStyle |= MS_SHOWMAGNIFIEDCURSOR;
        }

        m_impl->magHwnd = CreateWindowExW(
            0, WC_MAGNIFIER, L"", magStyle,
            0, 0, width, height,
            m_hwnd, nullptr, hInstance, nullptr);

        if (m_impl->magHwnd) {
            HWND exclude = m_hwnd;
            MagSetWindowFilterList(m_impl->magHwnd, MW_FILTERMODE_EXCLUDE, 1, &exclude);
            MagSetWindowSource(m_impl->magHwnd, m_bounds);

            MAGTRANSFORM transform = {};
            transform.v[0][0] = -1.0f;
            transform.v[1][1] = 1.0f;
            transform.v[2][0] = static_cast<float>(width);
            transform.v[2][2] = 1.0f;
            MagSetWindowTransform(m_impl->magHwnd, &transform);

            SetTimer(m_hwnd, Impl::TIMER_REFRESH, Impl::TIMER_REFRESH_MS, nullptr);
            LOG_INFO("MirrorWindow: using Magnification API fallback for %ws", deviceName.c_str());
        } else {
            LOG_ERROR("MirrorWindow: failed to create fallback magnifier control: %lu", GetLastError());
            DestroyWindow(m_hwnd);
            m_hwnd = nullptr;
            return false;
        }
    }

    SetWindowPos(m_hwnd, HWND_TOPMOST, bounds.left, bounds.top, width, height,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);

    LOG_INFO("MirrorWindow created successfully for %ws (%dx%d at %d,%d)",
             deviceName.c_str(), width, height, bounds.left, bounds.top);
    return true;
}

void MirrorWindow::UpdateBounds(const RECT& bounds, HMONITOR hMonitor) {
    if (!m_hwnd) {
        return;
    }
    if (EqualRect(&m_bounds, &bounds) && m_hMonitor == hMonitor) {
        return;
    }

    const HINSTANCE hInstance = m_hInstance;
    const std::wstring deviceName = m_deviceName;
    const bool magnified = m_magnifiedCursor;

    Destroy();
    m_magnifiedCursor = magnified;
    Create(hInstance, deviceName, bounds, hMonitor);
}

void MirrorWindow::SetMagnifiedCursor(bool enabled) {
    m_magnifiedCursor = enabled;
    if (m_impl) {
        m_impl->drawCursor = enabled;
    }

    if (m_impl && m_impl->magHwnd) {
        LONG_PTR style = GetWindowLongPtrW(m_impl->magHwnd, GWL_STYLE);
        if (enabled) {
            style |= MS_SHOWMAGNIFIEDCURSOR;
        } else {
            style &= ~MS_SHOWMAGNIFIEDCURSOR;
        }
        SetWindowLongPtrW(m_impl->magHwnd, GWL_STYLE, style);
        SetWindowPos(m_impl->magHwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
}

void MirrorWindow::RaiseToTop() {
    if (m_hwnd) {
        SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }
}

void MirrorWindow::Destroy() {
    if (m_impl) {
        m_impl->Cleanup(m_hwnd);
    }
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    m_deviceName.clear();
    m_bounds = {};
    m_hMonitor = nullptr;
}

LRESULT CALLBACK MirrorWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return 0;
        }

        case WM_TIMER: {
            if (wParam == Impl::TIMER_REFRESH) {
                auto* pThis = reinterpret_cast<MirrorWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
                if (pThis && pThis->m_impl && pThis->m_impl->magHwnd) {
                    InvalidateRect(pThis->m_impl->magHwnd, nullptr, FALSE);
                }
                return 0;
            }
            break;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_NCHITTEST:
            // Full click-through pass to underlying application windows
            return HTTRANSPARENT;

        case WM_DESTROY:
            return 0;

        default:
            break;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace VirtualOverlay
