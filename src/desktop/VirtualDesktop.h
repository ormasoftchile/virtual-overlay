#pragma once

#include "VirtualDesktopInterop.h"
#include <functional>
#include <string>
#include <wrl/client.h>
#include <ShObjIdl.h>  // For IVirtualDesktopManager (public API)

namespace VirtualOverlay {

using Microsoft::WRL::ComPtr;

// Callback for desktop switch events
using DesktopSwitchCallback = std::function<void(int desktopIndex, const std::wstring& desktopName)>;

// Desktop information
struct DesktopInfo {
    GUID id = {};
    int index = 0;
    std::wstring name;
};

class VirtualDesktop {
public:
    static VirtualDesktop& Instance();

    // Non-copyable, non-movable
    VirtualDesktop(const VirtualDesktop&) = delete;
    VirtualDesktop& operator=(const VirtualDesktop&) = delete;

    // Lifecycle
    bool Init();
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }
    bool IsAvailable() const { return m_available; }

    // Desktop queries
    bool GetCurrentDesktop(DesktopInfo& info);
    int GetDesktopCount();
    bool GetDesktopByIndex(int index, DesktopInfo& info);

    // Notification registration
    void SetDesktopSwitchCallback(DesktopSwitchCallback callback);
    void ClearDesktopSwitchCallback();

    // Expose version for debugging
    WindowsVirtualDesktopVersion GetWindowsVersion() const { return m_windowsVersion; }

private:
    VirtualDesktop();
    ~VirtualDesktop();

    // COM initialization
    bool InitializeCOM();
    bool AcquireVirtualDesktopInterfaces();
    void ReleaseInterfaces();

    // Version-specific helpers
    bool GetCurrentDesktopWin10(DesktopInfo& info);
    bool GetCurrentDesktopWin11_21H2(DesktopInfo& info);
    bool GetCurrentDesktopWin11_23H2(DesktopInfo& info);
    bool GetCurrentDesktopWin11_24H2_Preview(DesktopInfo& info);

    bool GetDesktopInfoWin10(IUnknown* pDesktop, DesktopInfo& info);
    bool GetDesktopInfoWin11_21H2(IUnknown* pDesktop, DesktopInfo& info);
    bool GetDesktopInfoWin11_23H2(IUnknown* pDesktop, DesktopInfo& info);
    bool GetDesktopInfoWin11_24H2_Preview(IUnknown* pDesktop, DesktopInfo& info);

    int GetDesktopIndexByGUID(const GUID& guid);
    int GetDesktopIndexFromPolling(const GUID& desktopId);
    std::wstring GetDesktopNameFromRegistry(const GUID& desktopId);
    bool GetCurrentDesktopIdFromRegistry(GUID& desktopId);
    std::wstring HStringToWString(HSTRING hstr);

    // Internal notification handler
    void OnDesktopSwitched();
    
    // Allow notification classes to trigger switch callback
    friend class VirtualDesktopNotification_Win10;
    friend class VirtualDesktopNotification_Win11_21H2;
    friend class VirtualDesktopNotification_Win11_23H2;
    friend class VirtualDesktopNotification_Win11_24H2_Preview;

    bool m_initialized = false;
    bool m_available = false;
    bool m_comOwned = false;  // True if we called CoInitialize
    bool m_usingPolling = false;  // True if we're using polling instead of notifications
    WindowsVirtualDesktopVersion m_windowsVersion = WindowsVirtualDesktopVersion::Unknown;
    
    // COM interfaces - using IUnknown because specific type varies by Windows version
    ComPtr<IServiceProvider> m_pServiceProvider;
    ComPtr<IUnknown> m_pVirtualDesktopManagerInternal;
    ComPtr<IVirtualDesktopNotificationService> m_pNotificationService;
    
    // Public API interface (always available)
    ComPtr<IVirtualDesktopManager> m_pPublicVirtualDesktopManager;
    
    // Notification cookie
    DWORD m_notificationCookie = 0;
    ComPtr<IUnknown> m_pNotificationHandler;
    
    // Polling fallback
    UINT_PTR m_pollingTimer = 0;
    GUID m_lastKnownDesktopId = {};
    int m_lastKnownDesktopIndex = 0;
    static void CALLBACK PollingTimerProc(HWND, UINT, UINT_PTR, DWORD);
    void CheckDesktopChange();
    
    // WinEvent hook for desktop switch detection
    HWINEVENTHOOK m_desktopSwitchHook = nullptr;
    static void CALLBACK WinEventProc(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD);
    
    // User callback
    DesktopSwitchCallback m_switchCallback;
};

// =============================================================================
// Notification Handler Classes (one per Windows version)
// =============================================================================

class VirtualDesktopNotification_Win10 : public IVirtualDesktopNotification_Win10 {
public:
    VirtualDesktopNotification_Win10() : m_refCount(1) {}

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    // IVirtualDesktopNotification_Win10
    HRESULT STDMETHODCALLTYPE VirtualDesktopCreated(IVirtualDesktop_Win10*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE VirtualDesktopDestroyBegin(IVirtualDesktop_Win10*, IVirtualDesktop_Win10*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE VirtualDesktopDestroyFailed(IVirtualDesktop_Win10*, IVirtualDesktop_Win10*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE VirtualDesktopDestroyed(IVirtualDesktop_Win10*, IVirtualDesktop_Win10*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ViewVirtualDesktopChanged(IApplicationView*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE CurrentVirtualDesktopChanged(IVirtualDesktop_Win10*, IVirtualDesktop_Win10* pNew) override;

private:
    ULONG m_refCount;
};

class VirtualDesktopNotification_Win11_21H2 : public IVirtualDesktopNotification_Win11_21H2 {
public:
    VirtualDesktopNotification_Win11_21H2() : m_refCount(1) {}

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    // IVirtualDesktopNotification_Win11_21H2
    HRESULT STDMETHODCALLTYPE VirtualDesktopCreated(IObjectArray*, IVirtualDesktop_Win11_21H2*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE VirtualDesktopDestroyBegin(IObjectArray*, IVirtualDesktop_Win11_21H2*, IVirtualDesktop_Win11_21H2*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE VirtualDesktopDestroyFailed(IObjectArray*, IVirtualDesktop_Win11_21H2*, IVirtualDesktop_Win11_21H2*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE VirtualDesktopDestroyed(IObjectArray*, IVirtualDesktop_Win11_21H2*, IVirtualDesktop_Win11_21H2*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE VirtualDesktopIsPerMonitorChanged(BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE VirtualDesktopMoved(IObjectArray*, IVirtualDesktop_Win11_21H2*, int, int) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE VirtualDesktopNameChanged(IVirtualDesktop_Win11_21H2*, HSTRING) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ViewVirtualDesktopChanged(IApplicationView*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE CurrentVirtualDesktopChanged(IObjectArray*, IVirtualDesktop_Win11_21H2*, IVirtualDesktop_Win11_21H2* pNew) override;
    HRESULT STDMETHODCALLTYPE VirtualDesktopWallpaperChanged(IVirtualDesktop_Win11_21H2*, HSTRING) override { return S_OK; }

private:
    ULONG m_refCount;
};

class VirtualDesktopNotification_Win11_23H2 : public IVirtualDesktopNotification_Win11_23H2 {
public:
    VirtualDesktopNotification_Win11_23H2() : m_refCount(1) {}

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    // IVirtualDesktopNotification_Win11_23H2
    HRESULT STDMETHODCALLTYPE VirtualDesktopCreated(IObjectArray*, IVirtualDesktop_Win11_23H2*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE VirtualDesktopDestroyBegin(IObjectArray*, IVirtualDesktop_Win11_23H2*, IVirtualDesktop_Win11_23H2*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE VirtualDesktopDestroyFailed(IObjectArray*, IVirtualDesktop_Win11_23H2*, IVirtualDesktop_Win11_23H2*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE VirtualDesktopDestroyed(IObjectArray*, IVirtualDesktop_Win11_23H2*, IVirtualDesktop_Win11_23H2*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE VirtualDesktopIsPerMonitorChanged(BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE VirtualDesktopMoved(IObjectArray*, IVirtualDesktop_Win11_23H2*, int, int) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE VirtualDesktopNameChanged(IVirtualDesktop_Win11_23H2*, HSTRING) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ViewVirtualDesktopChanged(IApplicationView*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE CurrentVirtualDesktopChanged(IObjectArray*, IVirtualDesktop_Win11_23H2*, IVirtualDesktop_Win11_23H2* pNew) override;
    HRESULT STDMETHODCALLTYPE VirtualDesktopWallpaperChanged(IVirtualDesktop_Win11_23H2*, HSTRING) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE VirtualDesktopSwitched(IVirtualDesktop_Win11_23H2*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE RemoteVirtualDesktopConnected(IVirtualDesktop_Win11_23H2*) override { return S_OK; }

private:
    ULONG m_refCount;
};

class VirtualDesktopNotification_Win11_24H2_Preview : public IVirtualDesktopNotification_Win11_24H2_Preview {
public:
    VirtualDesktopNotification_Win11_24H2_Preview() : m_refCount(1) {}

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    // IVirtualDesktopNotification_Win11_24H2_Preview
    HRESULT STDMETHODCALLTYPE VirtualDesktopCreated(IObjectArray*, IVirtualDesktop_Win11_24H2_Preview*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE VirtualDesktopDestroyBegin(IObjectArray*, IVirtualDesktop_Win11_24H2_Preview*, IVirtualDesktop_Win11_24H2_Preview*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE VirtualDesktopDestroyFailed(IObjectArray*, IVirtualDesktop_Win11_24H2_Preview*, IVirtualDesktop_Win11_24H2_Preview*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE VirtualDesktopDestroyed(IObjectArray*, IVirtualDesktop_Win11_24H2_Preview*, IVirtualDesktop_Win11_24H2_Preview*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE VirtualDesktopIsPerMonitorChanged(BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE VirtualDesktopMoved(IObjectArray*, IVirtualDesktop_Win11_24H2_Preview*, int, int) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE VirtualDesktopNameChanged(IVirtualDesktop_Win11_24H2_Preview*, HSTRING) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ViewVirtualDesktopChanged(IApplicationView*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE CurrentVirtualDesktopChanged(IObjectArray*, IVirtualDesktop_Win11_24H2_Preview*, IVirtualDesktop_Win11_24H2_Preview* pNew) override;
    HRESULT STDMETHODCALLTYPE VirtualDesktopWallpaperChanged(IVirtualDesktop_Win11_24H2_Preview*, HSTRING) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE VirtualDesktopSwitched(IVirtualDesktop_Win11_24H2_Preview*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE RemoteVirtualDesktopConnected(IVirtualDesktop_Win11_24H2_Preview*) override { return S_OK; }

private:
    ULONG m_refCount;
};

}  // namespace VirtualOverlay
