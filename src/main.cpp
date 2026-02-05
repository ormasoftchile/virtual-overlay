// Virtual Overlay - Entry Point
// Implements: WinMain, single-instance check, message loop, COM initialization

#include <windows.h>
#include <windowsx.h>  // For GET_X_LPARAM, GET_Y_LPARAM
#include <objbase.h>

#include "App.h"
#include "utils/Logger.h"
#include "config/Config.h"
#include "input/InputHandler.h"
#include "input/GestureHandler.h"
#include "tray/TrayIcon.h"

// Application name for mutex and window class
constexpr wchar_t APP_NAME[] = L"VirtualOverlay";
constexpr wchar_t MUTEX_NAME[] = L"Local\\VirtualOverlay_SingleInstance";
constexpr wchar_t WINDOW_CLASS[] = L"VirtualOverlayMainWindow";

// Custom message for bringing existing instance to foreground
constexpr UINT WM_BRINGTOFRONT = WM_USER + 1;

// Forward declarations
LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
bool CheckSingleInstance(HANDLE& hMutex);
bool RegisterMainWindowClass(HINSTANCE hInstance);
HWND CreateMainWindow(HINSTANCE hInstance);
int RunMessageLoop();

// Global handles
static HWND g_hMainWnd = nullptr;
static HINSTANCE g_hInstance = nullptr;

int WINAPI WinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPSTR lpCmdLine,
    _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(nCmdShow);

    g_hInstance = hInstance;

    // Initialize COM for virtual desktop API
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        MessageBoxW(nullptr, L"Failed to initialize COM", APP_NAME, MB_ICONERROR);
        return 1;
    }

    // Single instance check
    HANDLE hMutex = nullptr;
    if (!CheckSingleInstance(hMutex)) {
        // Another instance is running, try to activate it
        HWND hExisting = FindWindowW(WINDOW_CLASS, nullptr);
        if (hExisting) {
            PostMessageW(hExisting, WM_BRINGTOFRONT, 0, 0);
        }
        CoUninitialize();
        return 0;
    }

    int exitCode = 1;

    // Initialize logger
    if (VirtualOverlay::Logger::Instance().Init()) {
        LOG_INFO("Virtual Overlay starting...");

        // Load configuration
        VirtualOverlay::Config::Instance().Load();
        LOG_INFO("Configuration loaded");

        // Register window class
        if (RegisterMainWindowClass(hInstance)) {
            // Create hidden main window for message handling
            g_hMainWnd = CreateMainWindow(hInstance);
            if (g_hMainWnd) {
                // Initialize application controller
                if (VirtualOverlay::App::Instance().Init(hInstance, g_hMainWnd)) {
                    LOG_INFO("Application initialized successfully");

                    // Run message loop
                    exitCode = RunMessageLoop();

                    // Shutdown application
                    VirtualOverlay::App::Instance().Shutdown();

                    LOG_INFO("Application shutting down, exit code: {}", exitCode);
                } else {
                    LOG_ERROR("Failed to initialize application");
                }
            } else {
                LOG_ERROR("Failed to create main window");
            }
        } else {
            LOG_ERROR("Failed to register window class");
        }

        VirtualOverlay::Logger::Instance().Shutdown();
    }

    // Cleanup
    if (hMutex) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }

    CoUninitialize();
    return exitCode;
}

bool CheckSingleInstance(HANDLE& hMutex) {
    hMutex = CreateMutexW(nullptr, TRUE, MUTEX_NAME);
    if (hMutex == nullptr) {
        return false;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hMutex);
        hMutex = nullptr;
        return false;
    }

    return true;
}

bool RegisterMainWindowClass(HINSTANCE hInstance) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = WINDOW_CLASS;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(1));  // IDI_APP from resource
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);

    return RegisterClassExW(&wc) != 0;
}

HWND CreateMainWindow(HINSTANCE hInstance) {
    // Create a hidden window (NOT message-only, so it receives WM_DISPLAYCHANGE)
    // WS_EX_TOOLWINDOW prevents it from appearing in taskbar
    HWND hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        WINDOW_CLASS,
        APP_NAME,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT,
        nullptr,  // Desktop parent - receives broadcast messages like WM_DISPLAYCHANGE
        nullptr,
        hInstance,
        nullptr
    );

    return hwnd;
}

int RunMessageLoop() {
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        case WM_TIMER:
            if (wParam == VirtualOverlay::TIMER_ZOOM_UPDATE) {
                VirtualOverlay::App::Instance().OnZoomTimer();
            }
            return 0;

        case WM_BRINGTOFRONT:
            // Another instance tried to start - could open settings here
            LOG_DEBUG("Received bring-to-front request from another instance");
            VirtualOverlay::App::Instance().OpenSettings();
            return 0;

        case VirtualOverlay::WM_TRAYICON:
            VirtualOverlay::TrayIcon::Instance().HandleMessage(wParam, lParam);
            return 0;

        case WM_DISPLAYCHANGE:
            // Monitor configuration changed
            VirtualOverlay::App::Instance().OnDisplayChange();
            return 0;

        case WM_DPICHANGED: {
            // DPI changed
            UINT newDpi = HIWORD(wParam);
            RECT* suggested = reinterpret_cast<RECT*>(lParam);
            VirtualOverlay::App::Instance().OnDpiChanged(hwnd, newDpi, suggested);
            return 0;
        }

        case WM_GESTURE:
            if (VirtualOverlay::GestureHandler::Instance().ProcessGesture(hwnd, wParam, lParam)) {
                return 0;
            }
            break;

        // Zoom feature messages
        case VirtualOverlay::WM_USER_ZOOM_IN:
            VirtualOverlay::App::Instance().OnZoomIn();
            return 0;

        case VirtualOverlay::WM_USER_ZOOM_OUT:
            VirtualOverlay::App::Instance().OnZoomOut();
            return 0;

        case VirtualOverlay::WM_USER_ZOOM_RESET:
            VirtualOverlay::App::Instance().OnZoomReset();
            return 0;

        case VirtualOverlay::WM_USER_MODIFIER_DOWN:
            VirtualOverlay::App::Instance().OnModifierDown();
            return 0;

        case VirtualOverlay::WM_USER_MODIFIER_UP:
            VirtualOverlay::App::Instance().OnModifierUp();
            return 0;

        case WM_HOTKEY:
            if (wParam == VirtualOverlay::HOTKEY_OVERLAY_TOGGLE) {
                VirtualOverlay::App::Instance().OnToggleOverlay();
            }
            return 0;

        case VirtualOverlay::WM_USER_CURSOR_MOVE: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            VirtualOverlay::App::Instance().OnCursorMove(x, y);
            return 0;
        }

        default:
            break;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
