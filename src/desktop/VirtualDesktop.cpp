#define VIRTUALDESKTOP_INTEROP_DEFINE_GUIDS
#include "VirtualDesktop.h"
#include "../utils/Logger.h"
#include <roapi.h>
#include <winstring.h>
#include <vector>

#pragma comment(lib, "runtimeobject.lib")

namespace VirtualOverlay {

// Immersive Shell CLSID (declared before use)
extern const CLSID CLSID_ImmersiveShell;

// VirtualDesktopManagerInternal CLSID (declared before use, defined at end of file)
extern const CLSID CLSID_VirtualDesktopManagerInternal;

VirtualDesktop::VirtualDesktop() = default;

VirtualDesktop::~VirtualDesktop() {
    if (m_initialized) {
        Shutdown();
    }
}

VirtualDesktop& VirtualDesktop::Instance() {
    static VirtualDesktop instance;
    return instance;
}

bool VirtualDesktop::Init() {
    if (m_initialized) {
        return true;
    }

    // Detect Windows version
    m_windowsVersion = GetCurrentVirtualDesktopVersion();
    LOG_INFO("Detected Windows version for VirtualDesktop: %d", static_cast<int>(m_windowsVersion));

    if (m_windowsVersion == WindowsVirtualDesktopVersion::Unknown) {
        LOG_WARN("Unknown Windows version, virtual desktop support may not work");
    }

    // Initialize COM
    if (!InitializeCOM()) {
        LOG_ERROR("Failed to initialize COM for VirtualDesktop");
        return false;
    }

    // Always try to get the public IVirtualDesktopManager first (documented API)
    HRESULT hr = CoCreateInstance(
        CLSID_VirtualDesktopManager,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&m_pPublicVirtualDesktopManager)
    );
    
    if (SUCCEEDED(hr)) {
        LOG_INFO("Public IVirtualDesktopManager acquired successfully");
    } else {
        LOG_WARN("Failed to get public IVirtualDesktopManager: 0x%08X", hr);
    }

    // Try to acquire private interfaces for notifications
    if (!AcquireVirtualDesktopInterfaces()) {
        LOG_WARN("Failed to acquire internal virtual desktop interfaces");
        
        // Fall back to polling if public API is available
        if (m_pPublicVirtualDesktopManager) {
            m_usingPolling = true;
            m_available = true;
            LOG_INFO("Using polling fallback for desktop change detection");
        } else {
            m_available = false;
            LOG_WARN("Virtual desktop feature will be disabled");
        }
    } else {
        m_available = true;
        m_usingPolling = false;
        LOG_INFO("Virtual desktop interfaces acquired successfully (notification-based)");
    }

    m_initialized = true;
    return true;
}

void VirtualDesktop::Shutdown() {
    if (!m_initialized) {
        return;
    }

    // Stop polling timer if active
    if (m_pollingTimer != 0) {
        KillTimer(nullptr, m_pollingTimer);
        m_pollingTimer = 0;
    }

    // Unhook WinEvent
    if (m_desktopSwitchHook) {
        UnhookWinEvent(m_desktopSwitchHook);
        m_desktopSwitchHook = nullptr;
    }

    // Unregister notifications
    if (m_notificationCookie != 0 && m_pNotificationService) {
        m_pNotificationService->Unregister(m_notificationCookie);
        m_notificationCookie = 0;
    }

    ReleaseInterfaces();
    m_pPublicVirtualDesktopManager.Reset();

    if (m_comOwned) {
        CoUninitialize();
        m_comOwned = false;
    }

    m_initialized = false;
    m_available = false;
    m_usingPolling = false;
    LOG_INFO("VirtualDesktop shutdown complete");
}

bool VirtualDesktop::InitializeCOM() {
    // Try MTA first (preferred for shell COM)
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr)) {
        m_comOwned = true;
    } else if (hr == S_FALSE) {
        // Already initialized on this thread - that's fine
        m_comOwned = false;
    } else if (hr == RPC_E_CHANGED_MODE) {
        // Already initialized as STA, try STA
        hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (SUCCEEDED(hr)) {
            m_comOwned = true;
        } else if (hr == S_FALSE) {
            m_comOwned = false;
        } else {
            LOG_ERROR("Failed to initialize COM: 0x%08X", hr);
            return false;
        }
    } else {
        LOG_ERROR("Failed to initialize COM: 0x%08X", hr);
        return false;
    }

    return true;
}

bool VirtualDesktop::AcquireVirtualDesktopInterfaces() {
    HRESULT hr;

    // Create ImmersiveShell instance
    ComPtr<IUnknown> pImmersiveShell;
    hr = CoCreateInstance(
        CLSID_ImmersiveShell,
        nullptr,
        CLSCTX_LOCAL_SERVER,
        IID_PPV_ARGS(&pImmersiveShell)
    );
    
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create ImmersiveShell: 0x%08X", hr);
        return false;
    }

    // Get IServiceProvider
    hr = pImmersiveShell->QueryInterface(IID_PPV_ARGS(&m_pServiceProvider));
    if (FAILED(hr)) {
        LOG_ERROR("Failed to get IServiceProvider: 0x%08X", hr);
        return false;
    }

    // Get version-specific GUIDs
    VirtualDesktopGUIDs guids = VirtualDesktopGUIDs::ForVersion(m_windowsVersion);

    // Get IVirtualDesktopManagerInternal
    hr = m_pServiceProvider->QueryService(
        CLSID_VirtualDesktopManagerInternal,
        guids.iidVirtualDesktopManagerInternal,
        reinterpret_cast<void**>(m_pVirtualDesktopManagerInternal.GetAddressOf())
    );
    
    if (FAILED(hr)) {
        LOG_ERROR("Failed to get IVirtualDesktopManagerInternal: 0x%08X", hr);
        // Try with a fallback CLSID
        hr = m_pServiceProvider->QueryService(
            guids.iidVirtualDesktopManagerInternal,
            guids.iidVirtualDesktopManagerInternal,
            reinterpret_cast<void**>(m_pVirtualDesktopManagerInternal.GetAddressOf())
        );
        if (FAILED(hr)) {
            LOG_ERROR("Fallback also failed: 0x%08X", hr);
            return false;
        }
    }

    // Get notification service
    hr = m_pServiceProvider->QueryService(
        CLSID_VirtualDesktopNotificationService,
        __uuidof(IVirtualDesktopNotificationService),
        reinterpret_cast<void**>(m_pNotificationService.GetAddressOf())
    );
    
    if (FAILED(hr)) {
        LOG_WARN("Failed to get IVirtualDesktopNotificationService: 0x%08X", hr);
        // Non-fatal - we can still query desktops, just won't get notifications
    } else {
        // Register for notifications
        IUnknown* pNotification = nullptr;
        
        switch (m_windowsVersion) {
            case WindowsVirtualDesktopVersion::Win10_1803:
            case WindowsVirtualDesktopVersion::Win10_1809:
            case WindowsVirtualDesktopVersion::Win10_1903:
            case WindowsVirtualDesktopVersion::Win10_1909:
            case WindowsVirtualDesktopVersion::Win10_2004:
            case WindowsVirtualDesktopVersion::Win10_20H2:
            case WindowsVirtualDesktopVersion::Win10_21H1:
            case WindowsVirtualDesktopVersion::Win10_21H2:
            case WindowsVirtualDesktopVersion::Win10_22H2:
                pNotification = new VirtualDesktopNotification_Win10();
                break;
                
            case WindowsVirtualDesktopVersion::Win11_21H2:
            case WindowsVirtualDesktopVersion::Win11_22H2:
                pNotification = new VirtualDesktopNotification_Win11_21H2();
                break;
                
            case WindowsVirtualDesktopVersion::Win11_23H2:
            case WindowsVirtualDesktopVersion::Win11_24H2:
                pNotification = new VirtualDesktopNotification_Win11_23H2();
                break;
                
            case WindowsVirtualDesktopVersion::Win11_24H2_Preview:
            default:
                pNotification = new VirtualDesktopNotification_Win11_24H2_Preview();
                break;
        }
        
        m_pNotificationHandler.Attach(pNotification);
        
        hr = m_pNotificationService->Register(m_pNotificationHandler.Get(), &m_notificationCookie);
        if (FAILED(hr)) {
            LOG_WARN("Failed to register for desktop notifications: 0x%08X", hr);
            m_notificationCookie = 0;
        } else {
            LOG_DEBUG("Registered for desktop change notifications, cookie=%lu", m_notificationCookie);
        }
    }

    return true;
}

void VirtualDesktop::ReleaseInterfaces() {
    m_pNotificationHandler.Reset();
    m_pNotificationService.Reset();
    m_pVirtualDesktopManagerInternal.Reset();
    m_pServiceProvider.Reset();
}

bool VirtualDesktop::GetCurrentDesktop(DesktopInfo& info) {
    // If we have internal interfaces, use version-specific methods
    if (m_available && m_pVirtualDesktopManagerInternal && !m_usingPolling) {
        switch (m_windowsVersion) {
            case WindowsVirtualDesktopVersion::Win10_1803:
            case WindowsVirtualDesktopVersion::Win10_1809:
            case WindowsVirtualDesktopVersion::Win10_1903:
            case WindowsVirtualDesktopVersion::Win10_1909:
            case WindowsVirtualDesktopVersion::Win10_2004:
            case WindowsVirtualDesktopVersion::Win10_20H2:
            case WindowsVirtualDesktopVersion::Win10_21H1:
            case WindowsVirtualDesktopVersion::Win10_21H2:
            case WindowsVirtualDesktopVersion::Win10_22H2:
                return GetCurrentDesktopWin10(info);
                
            case WindowsVirtualDesktopVersion::Win11_21H2:
            case WindowsVirtualDesktopVersion::Win11_22H2:
                return GetCurrentDesktopWin11_21H2(info);
                
            case WindowsVirtualDesktopVersion::Win11_23H2:
            case WindowsVirtualDesktopVersion::Win11_24H2:
                return GetCurrentDesktopWin11_23H2(info);
                
            case WindowsVirtualDesktopVersion::Win11_24H2_Preview:
            default:
                return GetCurrentDesktopWin11_24H2_Preview(info);
        }
    }
    
    // Fallback: use polling-based desktop index or return default
    if (m_usingPolling) {
        // If we don't have a desktop ID yet, try to get it from foreground window
        if (IsEqualGUID(m_lastKnownDesktopId, GUID{}) && m_pPublicVirtualDesktopManager) {
            HWND hForeground = GetForegroundWindow();
            if (hForeground) {
                m_pPublicVirtualDesktopManager->GetWindowDesktopId(hForeground, &m_lastKnownDesktopId);
                if (!IsEqualGUID(m_lastKnownDesktopId, GUID{})) {
                    m_lastKnownDesktopIndex = GetDesktopIndexFromPolling(m_lastKnownDesktopId);
                }
            }
        }
        
        if (!IsEqualGUID(m_lastKnownDesktopId, GUID{})) {
            info.id = m_lastKnownDesktopId;
            info.index = m_lastKnownDesktopIndex;
            info.name = GetDesktopNameFromRegistry(m_lastKnownDesktopId);
            return true;
        }
    }
    
    // Last resort: return desktop 1 with no name
    info.index = 1;
    info.name = L"";
    info.id = {};
    return true;
}

bool VirtualDesktop::GetCurrentDesktopWin10(DesktopInfo& info) {
    auto pManager = static_cast<IVirtualDesktopManagerInternal_Win10*>(
        m_pVirtualDesktopManagerInternal.Get());
    
    ComPtr<IVirtualDesktop_Win10> pDesktop;
    HRESULT hr = pManager->GetCurrentDesktop(pDesktop.GetAddressOf());
    if (FAILED(hr)) {
        LOG_ERROR("GetCurrentDesktop failed: 0x%08X", hr);
        return false;
    }

    return GetDesktopInfoWin10(pDesktop.Get(), info);
}

bool VirtualDesktop::GetCurrentDesktopWin11_21H2(DesktopInfo& info) {
    auto pManager = static_cast<IVirtualDesktopManagerInternal_Win11_21H2*>(
        m_pVirtualDesktopManagerInternal.Get());
    
    ComPtr<IVirtualDesktop_Win11_21H2> pDesktop;
    // Pass nullptr for monitor to get current desktop on any monitor
    HRESULT hr = pManager->GetCurrentDesktop(nullptr, pDesktop.GetAddressOf());
    if (FAILED(hr)) {
        LOG_ERROR("GetCurrentDesktop failed: 0x%08X", hr);
        return false;
    }

    return GetDesktopInfoWin11_21H2(pDesktop.Get(), info);
}

bool VirtualDesktop::GetCurrentDesktopWin11_23H2(DesktopInfo& info) {
    auto pManager = static_cast<IVirtualDesktopManagerInternal_Win11_23H2*>(
        m_pVirtualDesktopManagerInternal.Get());
    
    ComPtr<IVirtualDesktop_Win11_23H2> pDesktop;
    HRESULT hr = pManager->GetCurrentDesktop(nullptr, pDesktop.GetAddressOf());
    if (FAILED(hr)) {
        LOG_ERROR("GetCurrentDesktop failed: 0x%08X", hr);
        return false;
    }

    return GetDesktopInfoWin11_23H2(pDesktop.Get(), info);
}

bool VirtualDesktop::GetCurrentDesktopWin11_24H2_Preview(DesktopInfo& info) {
    auto pManager = static_cast<IVirtualDesktopManagerInternal_Win11_24H2_Preview*>(
        m_pVirtualDesktopManagerInternal.Get());
    
    ComPtr<IVirtualDesktop_Win11_24H2_Preview> pDesktop;
    HRESULT hr = pManager->GetCurrentDesktop(nullptr, pDesktop.GetAddressOf());
    if (FAILED(hr)) {
        LOG_ERROR("GetCurrentDesktop failed: 0x%08X", hr);
        return false;
    }

    return GetDesktopInfoWin11_24H2_Preview(pDesktop.Get(), info);
}

bool VirtualDesktop::GetDesktopInfoWin10(IUnknown* pUnkDesktop, DesktopInfo& info) {
    auto pDesktop = static_cast<IVirtualDesktop_Win10*>(pUnkDesktop);
    
    HRESULT hr = pDesktop->GetID(&info.id);
    if (FAILED(hr)) {
        return false;
    }

    // Windows 10 doesn't support desktop names in the COM interface
    info.name = L"";
    info.index = GetDesktopIndexByGUID(info.id);
    
    return true;
}

bool VirtualDesktop::GetDesktopInfoWin11_21H2(IUnknown* pUnkDesktop, DesktopInfo& info) {
    auto pDesktop = static_cast<IVirtualDesktop_Win11_21H2*>(pUnkDesktop);
    
    HRESULT hr = pDesktop->GetID(&info.id);
    if (FAILED(hr)) {
        return false;
    }

    HSTRING hName = nullptr;
    hr = pDesktop->GetName(&hName);
    if (SUCCEEDED(hr) && hName) {
        info.name = HStringToWString(hName);
        WindowsDeleteString(hName);
    } else {
        info.name = L"";
    }

    info.index = GetDesktopIndexByGUID(info.id);
    
    return true;
}

bool VirtualDesktop::GetDesktopInfoWin11_23H2(IUnknown* pUnkDesktop, DesktopInfo& info) {
    auto pDesktop = static_cast<IVirtualDesktop_Win11_23H2*>(pUnkDesktop);
    
    HRESULT hr = pDesktop->GetID(&info.id);
    if (FAILED(hr)) {
        return false;
    }

    HSTRING hName = nullptr;
    hr = pDesktop->GetName(&hName);
    if (SUCCEEDED(hr) && hName) {
        info.name = HStringToWString(hName);
        WindowsDeleteString(hName);
    } else {
        info.name = L"";
    }

    info.index = GetDesktopIndexByGUID(info.id);
    
    return true;
}

bool VirtualDesktop::GetDesktopInfoWin11_24H2_Preview(IUnknown* pUnkDesktop, DesktopInfo& info) {
    auto pDesktop = static_cast<IVirtualDesktop_Win11_24H2_Preview*>(pUnkDesktop);
    
    HRESULT hr = pDesktop->GetID(&info.id);
    if (FAILED(hr)) {
        return false;
    }

    HSTRING hName = nullptr;
    hr = pDesktop->GetName(&hName);
    if (SUCCEEDED(hr) && hName) {
        info.name = HStringToWString(hName);
        WindowsDeleteString(hName);
    } else {
        info.name = L"";
    }

    info.index = GetDesktopIndexByGUID(info.id);
    
    return true;
}

int VirtualDesktop::GetDesktopCount() {
    if (!m_available || !m_pVirtualDesktopManagerInternal) {
        return 1;
    }

    UINT count = 0;
    HRESULT hr = E_FAIL;

    switch (m_windowsVersion) {
        case WindowsVirtualDesktopVersion::Win10_1803:
        case WindowsVirtualDesktopVersion::Win10_1809:
        case WindowsVirtualDesktopVersion::Win10_1903:
        case WindowsVirtualDesktopVersion::Win10_1909:
        case WindowsVirtualDesktopVersion::Win10_2004:
        case WindowsVirtualDesktopVersion::Win10_20H2:
        case WindowsVirtualDesktopVersion::Win10_21H1:
        case WindowsVirtualDesktopVersion::Win10_21H2:
        case WindowsVirtualDesktopVersion::Win10_22H2: {
            auto pManager = static_cast<IVirtualDesktopManagerInternal_Win10*>(
                m_pVirtualDesktopManagerInternal.Get());
            hr = pManager->GetCount(&count);
            break;
        }
            
        case WindowsVirtualDesktopVersion::Win11_21H2:
        case WindowsVirtualDesktopVersion::Win11_22H2: {
            auto pManager = static_cast<IVirtualDesktopManagerInternal_Win11_21H2*>(
                m_pVirtualDesktopManagerInternal.Get());
            hr = pManager->GetCount(nullptr, &count);
            break;
        }
            
        case WindowsVirtualDesktopVersion::Win11_23H2:
        case WindowsVirtualDesktopVersion::Win11_24H2: {
            auto pManager = static_cast<IVirtualDesktopManagerInternal_Win11_23H2*>(
                m_pVirtualDesktopManagerInternal.Get());
            hr = pManager->GetCount(nullptr, &count);
            break;
        }
            
        case WindowsVirtualDesktopVersion::Win11_24H2_Preview:
        default: {
            auto pManager = static_cast<IVirtualDesktopManagerInternal_Win11_24H2_Preview*>(
                m_pVirtualDesktopManagerInternal.Get());
            hr = pManager->GetCount(nullptr, &count);
            break;
        }
    }

    if (FAILED(hr)) {
        LOG_ERROR("GetCount failed: 0x%08X", hr);
        return 1;
    }

    return static_cast<int>(count);
}

int VirtualDesktop::GetDesktopIndexByGUID(const GUID& guid) {
    if (!m_available || !m_pVirtualDesktopManagerInternal) {
        return 1;
    }

    ComPtr<IObjectArray> pDesktops;
    HRESULT hr = E_FAIL;

    switch (m_windowsVersion) {
        case WindowsVirtualDesktopVersion::Win10_1803:
        case WindowsVirtualDesktopVersion::Win10_1809:
        case WindowsVirtualDesktopVersion::Win10_1903:
        case WindowsVirtualDesktopVersion::Win10_1909:
        case WindowsVirtualDesktopVersion::Win10_2004:
        case WindowsVirtualDesktopVersion::Win10_20H2:
        case WindowsVirtualDesktopVersion::Win10_21H1:
        case WindowsVirtualDesktopVersion::Win10_21H2:
        case WindowsVirtualDesktopVersion::Win10_22H2: {
            auto pManager = static_cast<IVirtualDesktopManagerInternal_Win10*>(
                m_pVirtualDesktopManagerInternal.Get());
            hr = pManager->GetDesktops(pDesktops.GetAddressOf());
            break;
        }
            
        case WindowsVirtualDesktopVersion::Win11_21H2:
        case WindowsVirtualDesktopVersion::Win11_22H2: {
            auto pManager = static_cast<IVirtualDesktopManagerInternal_Win11_21H2*>(
                m_pVirtualDesktopManagerInternal.Get());
            hr = pManager->GetDesktops(nullptr, pDesktops.GetAddressOf());
            break;
        }
            
        case WindowsVirtualDesktopVersion::Win11_23H2:
        case WindowsVirtualDesktopVersion::Win11_24H2: {
            auto pManager = static_cast<IVirtualDesktopManagerInternal_Win11_23H2*>(
                m_pVirtualDesktopManagerInternal.Get());
            hr = pManager->GetDesktops(nullptr, pDesktops.GetAddressOf());
            break;
        }
            
        case WindowsVirtualDesktopVersion::Win11_24H2_Preview:
        default: {
            auto pManager = static_cast<IVirtualDesktopManagerInternal_Win11_24H2_Preview*>(
                m_pVirtualDesktopManagerInternal.Get());
            hr = pManager->GetDesktops(nullptr, pDesktops.GetAddressOf());
            break;
        }
    }

    if (FAILED(hr) || !pDesktops) {
        return 1;
    }

    UINT count = 0;
    pDesktops->GetCount(&count);

    for (UINT i = 0; i < count; i++) {
        ComPtr<IUnknown> pDesktopUnk;
        if (FAILED(pDesktops->GetAt(i, IID_PPV_ARGS(&pDesktopUnk)))) {
            continue;
        }

        GUID desktopId = {};
        
        switch (m_windowsVersion) {
            case WindowsVirtualDesktopVersion::Win10_1803:
            case WindowsVirtualDesktopVersion::Win10_1809:
            case WindowsVirtualDesktopVersion::Win10_1903:
            case WindowsVirtualDesktopVersion::Win10_1909:
            case WindowsVirtualDesktopVersion::Win10_2004:
            case WindowsVirtualDesktopVersion::Win10_20H2:
            case WindowsVirtualDesktopVersion::Win10_21H1:
            case WindowsVirtualDesktopVersion::Win10_21H2:
            case WindowsVirtualDesktopVersion::Win10_22H2: {
                ComPtr<IVirtualDesktop_Win10> pDesktop;
                VirtualDesktopGUIDs guids = VirtualDesktopGUIDs::ForVersion(m_windowsVersion);
                if (SUCCEEDED(pDesktopUnk->QueryInterface(guids.iidVirtualDesktop, reinterpret_cast<void**>(pDesktop.ReleaseAndGetAddressOf())))) {
                    pDesktop->GetID(&desktopId);
                }
                break;
            }
                
            case WindowsVirtualDesktopVersion::Win11_21H2:
            case WindowsVirtualDesktopVersion::Win11_22H2: {
                ComPtr<IVirtualDesktop_Win11_21H2> pDesktop;
                VirtualDesktopGUIDs guids = VirtualDesktopGUIDs::ForVersion(m_windowsVersion);
                if (SUCCEEDED(pDesktopUnk->QueryInterface(guids.iidVirtualDesktop, reinterpret_cast<void**>(pDesktop.ReleaseAndGetAddressOf())))) {
                    pDesktop->GetID(&desktopId);
                }
                break;
            }
                
            case WindowsVirtualDesktopVersion::Win11_23H2:
            case WindowsVirtualDesktopVersion::Win11_24H2: {
                ComPtr<IVirtualDesktop_Win11_23H2> pDesktop;
                VirtualDesktopGUIDs guids = VirtualDesktopGUIDs::ForVersion(m_windowsVersion);
                if (SUCCEEDED(pDesktopUnk->QueryInterface(guids.iidVirtualDesktop, reinterpret_cast<void**>(pDesktop.ReleaseAndGetAddressOf())))) {
                    pDesktop->GetID(&desktopId);
                }
                break;
            }
                
            case WindowsVirtualDesktopVersion::Win11_24H2_Preview:
            default: {
                ComPtr<IVirtualDesktop_Win11_24H2_Preview> pDesktop;
                VirtualDesktopGUIDs guids = VirtualDesktopGUIDs::ForVersion(m_windowsVersion);
                if (SUCCEEDED(pDesktopUnk->QueryInterface(guids.iidVirtualDesktop, reinterpret_cast<void**>(pDesktop.ReleaseAndGetAddressOf())))) {
                    pDesktop->GetID(&desktopId);
                }
                break;
            }
        }

        if (IsEqualGUID(desktopId, guid)) {
            return static_cast<int>(i + 1);  // 1-based index
        }
    }

    return 1;
}

bool VirtualDesktop::GetDesktopByIndex(int index, DesktopInfo& info) {
    if (!m_available || !m_pVirtualDesktopManagerInternal) {
        info.index = index;
        info.name = L"";
        info.id = {};
        return true;
    }

    ComPtr<IObjectArray> pDesktops;
    HRESULT hr = E_FAIL;

    // Get desktops array based on Windows version
    switch (m_windowsVersion) {
        case WindowsVirtualDesktopVersion::Win10_1803:
        case WindowsVirtualDesktopVersion::Win10_1809:
        case WindowsVirtualDesktopVersion::Win10_1903:
        case WindowsVirtualDesktopVersion::Win10_1909:
        case WindowsVirtualDesktopVersion::Win10_2004:
        case WindowsVirtualDesktopVersion::Win10_20H2:
        case WindowsVirtualDesktopVersion::Win10_21H1:
        case WindowsVirtualDesktopVersion::Win10_21H2:
        case WindowsVirtualDesktopVersion::Win10_22H2: {
            auto pManager = static_cast<IVirtualDesktopManagerInternal_Win10*>(
                m_pVirtualDesktopManagerInternal.Get());
            hr = pManager->GetDesktops(pDesktops.GetAddressOf());
            break;
        }
            
        case WindowsVirtualDesktopVersion::Win11_21H2:
        case WindowsVirtualDesktopVersion::Win11_22H2: {
            auto pManager = static_cast<IVirtualDesktopManagerInternal_Win11_21H2*>(
                m_pVirtualDesktopManagerInternal.Get());
            hr = pManager->GetDesktops(nullptr, pDesktops.GetAddressOf());
            break;
        }
            
        case WindowsVirtualDesktopVersion::Win11_23H2:
        case WindowsVirtualDesktopVersion::Win11_24H2: {
            auto pManager = static_cast<IVirtualDesktopManagerInternal_Win11_23H2*>(
                m_pVirtualDesktopManagerInternal.Get());
            hr = pManager->GetDesktops(nullptr, pDesktops.GetAddressOf());
            break;
        }
            
        case WindowsVirtualDesktopVersion::Win11_24H2_Preview:
        default: {
            auto pManager = static_cast<IVirtualDesktopManagerInternal_Win11_24H2_Preview*>(
                m_pVirtualDesktopManagerInternal.Get());
            hr = pManager->GetDesktops(nullptr, pDesktops.GetAddressOf());
            break;
        }
    }

    if (FAILED(hr) || !pDesktops) {
        return false;
    }

    UINT count = 0;
    pDesktops->GetCount(&count);

    // Convert to 0-based index
    UINT idx = static_cast<UINT>(index - 1);
    if (idx >= count) {
        return false;
    }

    ComPtr<IUnknown> pDesktopUnk;
    if (FAILED(pDesktops->GetAt(idx, IID_PPV_ARGS(&pDesktopUnk)))) {
        return false;
    }

    info.index = index;

    switch (m_windowsVersion) {
        case WindowsVirtualDesktopVersion::Win10_1803:
        case WindowsVirtualDesktopVersion::Win10_1809:
        case WindowsVirtualDesktopVersion::Win10_1903:
        case WindowsVirtualDesktopVersion::Win10_1909:
        case WindowsVirtualDesktopVersion::Win10_2004:
        case WindowsVirtualDesktopVersion::Win10_20H2:
        case WindowsVirtualDesktopVersion::Win10_21H1:
        case WindowsVirtualDesktopVersion::Win10_21H2:
        case WindowsVirtualDesktopVersion::Win10_22H2:
            return GetDesktopInfoWin10(pDesktopUnk.Get(), info);
            
        case WindowsVirtualDesktopVersion::Win11_21H2:
        case WindowsVirtualDesktopVersion::Win11_22H2:
            return GetDesktopInfoWin11_21H2(pDesktopUnk.Get(), info);
            
        case WindowsVirtualDesktopVersion::Win11_23H2:
        case WindowsVirtualDesktopVersion::Win11_24H2:
            return GetDesktopInfoWin11_23H2(pDesktopUnk.Get(), info);
            
        case WindowsVirtualDesktopVersion::Win11_24H2_Preview:
        default:
            return GetDesktopInfoWin11_24H2_Preview(pDesktopUnk.Get(), info);
    }
}

std::wstring VirtualDesktop::HStringToWString(HSTRING hstr) {
    if (!hstr) return L"";
    
    UINT32 len = 0;
    const wchar_t* raw = WindowsGetStringRawBuffer(hstr, &len);
    if (raw && len > 0) {
        return std::wstring(raw, len);
    }
    return L"";
}

int VirtualDesktop::GetDesktopIndexFromPolling(const GUID& desktopId) {
    // In polling mode, we can't get the actual index without internal COM interfaces
    // Use registry to enumerate virtual desktop GUIDs and find the index
    
    HKEY hKey = nullptr;
    LONG result = RegOpenKeyExW(
        HKEY_CURRENT_USER,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VirtualDesktops",
        0,
        KEY_READ,
        &hKey
    );
    
    if (result != ERROR_SUCCESS) {
        return 1;  // Default to desktop 1
    }
    
    // Read VirtualDesktopIDs value (binary blob of GUIDs)
    DWORD dataSize = 0;
    result = RegQueryValueExW(hKey, L"VirtualDesktopIDs", nullptr, nullptr, nullptr, &dataSize);
    
    if (result != ERROR_SUCCESS || dataSize == 0 || dataSize % sizeof(GUID) != 0) {
        RegCloseKey(hKey);
        return 1;
    }
    
    std::vector<BYTE> data(dataSize);
    result = RegQueryValueExW(hKey, L"VirtualDesktopIDs", nullptr, nullptr, data.data(), &dataSize);
    RegCloseKey(hKey);
    
    if (result != ERROR_SUCCESS) {
        return 1;
    }
    
    // Search for matching GUID
    int numDesktops = static_cast<int>(dataSize / sizeof(GUID));
    const GUID* guids = reinterpret_cast<const GUID*>(data.data());
    
    for (int i = 0; i < numDesktops; i++) {
        if (IsEqualGUID(guids[i], desktopId)) {
            return i + 1;  // 1-based index
        }
    }
    
    return 1;  // Not found, default to 1
}

std::wstring VirtualDesktop::GetDesktopNameFromRegistry(const GUID& desktopId) {
    // Convert GUID to string for registry key path
    wchar_t guidStr[64] = {};
    StringFromGUID2(desktopId, guidStr, 64);
    
    // Build registry path: HKCU\SOFTWARE\Microsoft\Windows\CurrentVersion\Explorer\VirtualDesktops\Desktops\{GUID}
    std::wstring keyPath = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VirtualDesktops\\Desktops\\";
    keyPath += guidStr;
    
    HKEY hKey = nullptr;
    LONG result = RegOpenKeyExW(HKEY_CURRENT_USER, keyPath.c_str(), 0, KEY_READ, &hKey);
    
    if (result == ERROR_SUCCESS) {
        // Read "Name" value
        wchar_t nameBuffer[256] = {};
        DWORD nameSize = sizeof(nameBuffer);
        DWORD type = 0;
        
        result = RegQueryValueExW(hKey, L"Name", nullptr, &type, reinterpret_cast<LPBYTE>(nameBuffer), &nameSize);
        RegCloseKey(hKey);
        
        if (result == ERROR_SUCCESS && type == REG_SZ && nameSize > 2) {
            return std::wstring(nameBuffer);
        }
    }
    
    // No custom name set - return "Desktop N" as fallback
    int index = GetDesktopIndexFromPolling(desktopId);
    return L"Desktop " + std::to_wstring(index);
}

void VirtualDesktop::SetDesktopSwitchCallback(DesktopSwitchCallback callback) {
    m_switchCallback = std::move(callback);
    
    if (m_usingPolling && m_switchCallback) {
        // Initialize current desktop ID for change detection
        HWND hForeground = GetForegroundWindow();
        if (hForeground && m_pPublicVirtualDesktopManager) {
            m_pPublicVirtualDesktopManager->GetWindowDesktopId(hForeground, &m_lastKnownDesktopId);
        }
        m_lastKnownDesktopIndex = 1;
        
        // Use polling timer to detect desktop changes via public API
        // The undocumented WinEvent 0x0020 doesn't work reliably
        m_pollingTimer = SetTimer(nullptr, 0, 150, PollingTimerProc);
        if (m_pollingTimer) {
            LOG_INFO("Started desktop polling timer for change detection");
        } else {
            LOG_ERROR("Failed to start polling timer: %lu", GetLastError());
        }
    }
}

void VirtualDesktop::ClearDesktopSwitchCallback() {
    m_switchCallback = nullptr;
    
    if (m_desktopSwitchHook) {
        UnhookWinEvent(m_desktopSwitchHook);
        m_desktopSwitchHook = nullptr;
    }
    
    if (m_pollingTimer != 0) {
        KillTimer(nullptr, m_pollingTimer);
        m_pollingTimer = 0;
    }
}

void CALLBACK VirtualDesktop::WinEventProc(HWINEVENTHOOK, DWORD event, HWND, LONG, LONG, DWORD, DWORD) {
    if (event == 0x0020) {
        VirtualDesktop& vd = VirtualDesktop::Instance();
        vd.m_lastKnownDesktopIndex = (vd.m_lastKnownDesktopIndex % 10) + 1;
        vd.OnDesktopSwitched();
    }
}

void CALLBACK VirtualDesktop::PollingTimerProc(HWND, UINT, UINT_PTR, DWORD) {
    VirtualDesktop::Instance().CheckDesktopChange();
}

void VirtualDesktop::CheckDesktopChange() {
    if (!m_pPublicVirtualDesktopManager || !m_switchCallback) {
        return;
    }
    
    // Get any visible window to check its desktop ID
    HWND hWnd = GetForegroundWindow();
    if (!hWnd) {
        // Fallback to shell window
        hWnd = GetShellWindow();
    }
    if (!hWnd) {
        return;
    }
    
    // Check if the window is on the current virtual desktop
    BOOL isOnCurrent = FALSE;
    HRESULT hr = m_pPublicVirtualDesktopManager->IsWindowOnCurrentVirtualDesktop(hWnd, &isOnCurrent);
    
    if (FAILED(hr)) {
        return;
    }
    
    // Get the desktop ID for this window
    GUID currentDesktopId = {};
    hr = m_pPublicVirtualDesktopManager->GetWindowDesktopId(hWnd, &currentDesktopId);
    if (FAILED(hr) || IsEqualGUID(currentDesktopId, GUID{})) {
        // Window might be on all desktops - try a different approach
        // Enumerate top-level windows to find one with a valid desktop ID
        struct EnumData {
            IVirtualDesktopManager* pMgr;
            GUID desktopId;
            bool found;
        } data = { m_pPublicVirtualDesktopManager.Get(), {}, false };
        
        EnumWindows([](HWND hw, LPARAM lp) -> BOOL {
            auto* pData = reinterpret_cast<EnumData*>(lp);
            if (!IsWindowVisible(hw)) return TRUE;
            
            GUID id = {};
            if (SUCCEEDED(pData->pMgr->GetWindowDesktopId(hw, &id)) && !IsEqualGUID(id, GUID{})) {
                BOOL onCurrent = FALSE;
                if (SUCCEEDED(pData->pMgr->IsWindowOnCurrentVirtualDesktop(hw, &onCurrent)) && onCurrent) {
                    pData->desktopId = id;
                    pData->found = true;
                    return FALSE;  // Stop enumeration
                }
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&data));
        
        if (data.found) {
            currentDesktopId = data.desktopId;
        } else {
            return;  // Can't determine current desktop
        }
    }
    
    if (!IsEqualGUID(currentDesktopId, m_lastKnownDesktopId)) {
        GUID oldId = m_lastKnownDesktopId;
        m_lastKnownDesktopId = currentDesktopId;
        
        // Calculate actual desktop index by enumerating desktops
        m_lastKnownDesktopIndex = GetDesktopIndexFromPolling(currentDesktopId);
        
        LOG_DEBUG("Desktop change detected: polling found new desktop ID, index=%d", m_lastKnownDesktopIndex);
        OnDesktopSwitched();
    }
}

void VirtualDesktop::OnDesktopSwitched() {
    if (!m_switchCallback) {
        return;
    }

    DesktopInfo info;
    if (GetCurrentDesktop(info)) {
        LOG_DEBUG("Desktop switched to: %d (%ws)", info.index, info.name.c_str());
        m_switchCallback(info.index, info.name);
    }
}

// =============================================================================
// Notification Handler Implementations
// =============================================================================

// Windows 10
HRESULT STDMETHODCALLTYPE VirtualDesktopNotification_Win10::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    
    if (riid == IID_IUnknown || riid == __uuidof(IVirtualDesktopNotification_Win10)) {
        *ppv = static_cast<IVirtualDesktopNotification_Win10*>(this);
        AddRef();
        return S_OK;
    }
    
    *ppv = nullptr;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE VirtualDesktopNotification_Win10::AddRef() {
    return InterlockedIncrement(&m_refCount);
}

ULONG STDMETHODCALLTYPE VirtualDesktopNotification_Win10::Release() {
    ULONG count = InterlockedDecrement(&m_refCount);
    if (count == 0) {
        delete this;
    }
    return count;
}

HRESULT STDMETHODCALLTYPE VirtualDesktopNotification_Win10::CurrentVirtualDesktopChanged(
    IVirtualDesktop_Win10*, IVirtualDesktop_Win10*) {
    VirtualDesktop::Instance().OnDesktopSwitched();
    return S_OK;
}

// Windows 11 21H2/22H2
HRESULT STDMETHODCALLTYPE VirtualDesktopNotification_Win11_21H2::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    
    if (riid == IID_IUnknown || riid == __uuidof(IVirtualDesktopNotification_Win11_21H2)) {
        *ppv = static_cast<IVirtualDesktopNotification_Win11_21H2*>(this);
        AddRef();
        return S_OK;
    }
    
    *ppv = nullptr;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE VirtualDesktopNotification_Win11_21H2::AddRef() {
    return InterlockedIncrement(&m_refCount);
}

ULONG STDMETHODCALLTYPE VirtualDesktopNotification_Win11_21H2::Release() {
    ULONG count = InterlockedDecrement(&m_refCount);
    if (count == 0) {
        delete this;
    }
    return count;
}

HRESULT STDMETHODCALLTYPE VirtualDesktopNotification_Win11_21H2::CurrentVirtualDesktopChanged(
    IObjectArray*, IVirtualDesktop_Win11_21H2*, IVirtualDesktop_Win11_21H2*) {
    VirtualDesktop::Instance().OnDesktopSwitched();
    return S_OK;
}

// Windows 11 23H2+
HRESULT STDMETHODCALLTYPE VirtualDesktopNotification_Win11_23H2::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    
    if (riid == IID_IUnknown || riid == __uuidof(IVirtualDesktopNotification_Win11_23H2)) {
        *ppv = static_cast<IVirtualDesktopNotification_Win11_23H2*>(this);
        AddRef();
        return S_OK;
    }
    
    *ppv = nullptr;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE VirtualDesktopNotification_Win11_23H2::AddRef() {
    return InterlockedIncrement(&m_refCount);
}

ULONG STDMETHODCALLTYPE VirtualDesktopNotification_Win11_23H2::Release() {
    ULONG count = InterlockedDecrement(&m_refCount);
    if (count == 0) {
        delete this;
    }
    return count;
}

HRESULT STDMETHODCALLTYPE VirtualDesktopNotification_Win11_23H2::CurrentVirtualDesktopChanged(
    IObjectArray*, IVirtualDesktop_Win11_23H2*, IVirtualDesktop_Win11_23H2*) {
    VirtualDesktop::Instance().OnDesktopSwitched();
    return S_OK;
}

// Windows 11 24H2 Preview (Build 26200+)
HRESULT STDMETHODCALLTYPE VirtualDesktopNotification_Win11_24H2_Preview::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    
    if (riid == IID_IUnknown || riid == __uuidof(IVirtualDesktopNotification_Win11_24H2_Preview)) {
        *ppv = static_cast<IVirtualDesktopNotification_Win11_24H2_Preview*>(this);
        AddRef();
        return S_OK;
    }
    
    *ppv = nullptr;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE VirtualDesktopNotification_Win11_24H2_Preview::AddRef() {
    return InterlockedIncrement(&m_refCount);
}

ULONG STDMETHODCALLTYPE VirtualDesktopNotification_Win11_24H2_Preview::Release() {
    ULONG count = InterlockedDecrement(&m_refCount);
    if (count == 0) {
        delete this;
    }
    return count;
}

HRESULT STDMETHODCALLTYPE VirtualDesktopNotification_Win11_24H2_Preview::CurrentVirtualDesktopChanged(
    IObjectArray*, IVirtualDesktop_Win11_24H2_Preview*, IVirtualDesktop_Win11_24H2_Preview*) {
    VirtualDesktop::Instance().OnDesktopSwitched();
    return S_OK;
}

// CLSID used for getting internal virtual desktop manager interface
// {c2f03a33-21f5-47fa-b4bb-156362a2f239}
const CLSID CLSID_VirtualDesktopManagerInternal = 
    { 0xc2f03a33, 0x21f5, 0x47fa, { 0xb4, 0xbb, 0x15, 0x63, 0x62, 0xa2, 0xf2, 0x39 } };

}  // namespace VirtualOverlay
