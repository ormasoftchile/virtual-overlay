#define VIRTUALDESKTOP_INTEROP_DEFINE_GUIDS
#include "VirtualDesktop.h"
#include "../utils/Logger.h"
#include <roapi.h>
#include <winstring.h>
#include <algorithm>
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

    // Always use polling mode for reliability across different Windows builds
    // The internal COM interfaces have undocumented GUIDs that change frequently
    if (m_pPublicVirtualDesktopManager) {
        m_usingPolling = true;
        m_available = true;
        LOG_INFO("Using polling mode for desktop change detection (reliable across Windows versions)");
    } else {
        // No public API available - try internal interfaces as last resort
        if (!AcquireVirtualDesktopInterfaces()) {
            LOG_WARN("Failed to acquire virtual desktop interfaces");
            m_available = false;
            LOG_WARN("Virtual desktop feature will be disabled");
        } else {
            m_available = true;
            m_usingPolling = false;
            LOG_INFO("Virtual desktop interfaces acquired (notification-based)");
        }
    }

    // Check if the shell (explorer.exe) is ready — virtual desktop COM
    // interfaces depend on the shell and may not function during early boot
    // even if CoCreateInstance succeeds (the DLL loads but the service isn't up).
    if (!IsShellReady()) {
        LOG_WARN("Shell not running yet (early boot?) — will retry COM init when shell is ready");
        m_needsReinit = true;
    } else if (m_pPublicVirtualDesktopManager) {
        // Shell is running — verify the interface is actually functional
        HWND hShell = GetShellWindow();
        if (hShell) {
            BOOL isOnCurrent = FALSE;
            HRESULT hrCheck = m_pPublicVirtualDesktopManager->IsWindowOnCurrentVirtualDesktop(
                hShell, &isOnCurrent);
            if (FAILED(hrCheck)) {
                LOG_WARN("IVirtualDesktopManager acquired but not functional (0x%08X) — will retry",
                         hrCheck);
                m_pPublicVirtualDesktopManager.Reset();
                m_available = false;
                m_usingPolling = false;
                m_needsReinit = true;
            }
        }
    }

    // If no interfaces are available at all, still mark for reinit as a safety net
    if (!m_available && !m_needsReinit) {
        m_needsReinit = true;
    }

    // Verify registry-based detection works
    GUID testGuid = {};
    if (GetCurrentDesktopIdFromRegistry(testGuid)) {
        wchar_t guidStr[64] = {};
        StringFromGUID2(testGuid, guidStr, 64);
        LOG_INFO("Registry desktop detection OK: %ws", guidStr);
        
        int idx = GetDesktopIndexFromPolling(testGuid);
        std::wstring name = GetDesktopNameFromRegistry(testGuid);
        LOG_INFO("Current desktop from registry: index=%d, name=%ws", idx, name.c_str());
    } else {
        LOG_WARN("Registry desktop detection FAILED - CurrentVirtualDesktop not found");
    }

    m_initialized = true;
    return true;
}

void VirtualDesktop::Shutdown() {
    if (!m_initialized) {
        return;
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
    m_needsReinit = false;
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

bool VirtualDesktop::IsShellReady() {
    return GetShellWindow() != nullptr;
}

bool VirtualDesktop::TryReinitialize() {
    // Throttle attempts — don't hammer COM creation every 150ms poll
    DWORD now = GetTickCount();
    if (now - m_lastReinitAttemptTick < REINIT_INTERVAL_MS) {
        return false;
    }
    m_lastReinitAttemptTick = now;

    if (!IsShellReady()) {
        return false;
    }

    LOG_INFO("Shell is ready — attempting virtual desktop COM re-initialization...");

    // Acquire public IVirtualDesktopManager if we don't have it
    if (!m_pPublicVirtualDesktopManager) {
        HRESULT hr = CoCreateInstance(
            CLSID_VirtualDesktopManager,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&m_pPublicVirtualDesktopManager)
        );
        if (FAILED(hr)) {
            LOG_WARN("Reinit: CoCreateInstance for IVirtualDesktopManager failed: 0x%08X", hr);
            return false;
        }
        LOG_INFO("Reinit: IVirtualDesktopManager created");
    }

    // Verify the interface is actually functional
    HWND hShell = GetShellWindow();
    if (hShell) {
        BOOL isOnCurrent = FALSE;
        HRESULT hr = m_pPublicVirtualDesktopManager->IsWindowOnCurrentVirtualDesktop(
            hShell, &isOnCurrent);
        if (FAILED(hr)) {
            LOG_WARN("Reinit: IVirtualDesktopManager still not functional: 0x%08X", hr);
            m_pPublicVirtualDesktopManager.Reset();
            return false;
        }
    }

    m_usingPolling = true;
    m_available = true;
    m_needsReinit = false;

    // Refresh tracked desktop state
    if (GetCurrentDesktopIdFromRegistry(m_lastKnownDesktopId)) {
        m_lastKnownDesktopIndex = GetDesktopIndexFromPolling(m_lastKnownDesktopId);
        m_lastKnownDesktopName = GetDesktopNameFromRegistry(m_lastKnownDesktopId);
        UpdateTrackedForegroundWindow(m_lastKnownDesktopId);
    }

    LOG_INFO("Virtual desktop re-initialization successful (index=%d, name=%ws, tracked=%p)",
             m_lastKnownDesktopIndex, m_lastKnownDesktopName.c_str(), m_lastKnownForegroundHwnd);

    // Force-update the overlay with correct desktop info
    if (m_switchCallback) {
        m_switchCallback(m_lastKnownDesktopIndex, m_lastKnownDesktopName);
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
        // Always read fresh from registry
        GUID currentId = {};
        if (GetCurrentDesktopIdFromRegistry(currentId)) {
            info.id = currentId;
            info.index = GetDesktopIndexFromPolling(currentId);
            info.name = GetDesktopNameFromRegistry(currentId);
            return true;
        }
        
        // Try public VirtualDesktopManager API
        if (m_pPublicVirtualDesktopManager) {
            HWND hForeground = GetForegroundWindow();
            if (hForeground) {
                GUID desktopId = {};
                if (SUCCEEDED(m_pPublicVirtualDesktopManager->GetWindowDesktopId(hForeground, &desktopId)) 
                    && !IsEqualGUID(desktopId, GUID{})) {
                    info.id = desktopId;
                    info.index = GetDesktopIndexFromPolling(desktopId);
                    info.name = GetDesktopNameFromRegistry(desktopId);
                    return true;
                }
            }
        }
        
        // Use cached values if available
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

bool VirtualDesktop::GetCurrentDesktopIdFromRegistry(GUID& desktopId) {
    // Read CurrentVirtualDesktop from registry - this is the most reliable method
    HKEY hKey = nullptr;
    LONG result = RegOpenKeyExW(
        HKEY_CURRENT_USER,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VirtualDesktops",
        0,
        KEY_READ,
        &hKey
    );
    
    if (result != ERROR_SUCCESS) {
        return false;
    }
    
    DWORD dataSize = sizeof(GUID);
    DWORD type = 0;
    result = RegQueryValueExW(hKey, L"CurrentVirtualDesktop", nullptr, &type, 
                              reinterpret_cast<LPBYTE>(&desktopId), &dataSize);
    RegCloseKey(hKey);
    
    return (result == ERROR_SUCCESS && type == REG_BINARY && dataSize == sizeof(GUID));
}

std::vector<GUID> VirtualDesktop::GetAllDesktopIdsFromRegistry() {
    std::vector<GUID> result;
    
    HKEY hKey = nullptr;
    LONG status = RegOpenKeyExW(
        HKEY_CURRENT_USER,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VirtualDesktops",
        0,
        KEY_READ,
        &hKey
    );
    
    if (status != ERROR_SUCCESS) {
        return result;
    }
    
    DWORD dataSize = 0;
    status = RegQueryValueExW(hKey, L"VirtualDesktopIDs", nullptr, nullptr, nullptr, &dataSize);
    
    if (status != ERROR_SUCCESS || dataSize == 0 || dataSize % sizeof(GUID) != 0) {
        RegCloseKey(hKey);
        return result;
    }
    
    std::vector<BYTE> data(dataSize);
    status = RegQueryValueExW(hKey, L"VirtualDesktopIDs", nullptr, nullptr, data.data(), &dataSize);
    RegCloseKey(hKey);
    
    if (status != ERROR_SUCCESS) {
        return result;
    }
    
    int count = static_cast<int>(dataSize / sizeof(GUID));
    const GUID* guids = reinterpret_cast<const GUID*>(data.data());
    result.assign(guids, guids + count);
    return result;
}

GUID VirtualDesktop::FindCurrentDesktopByElimination() {
    if (!m_pPublicVirtualDesktopManager) {
        return {};
    }
    
    std::vector<GUID> allDesktops = GetAllDesktopIdsFromRegistry();
    if (allDesktops.empty()) {
        return {};
    }
    
    // First pass: try to find a window that IS on the current desktop
    // and has a valid (non-null) desktop ID. This handles the case where
    // the target desktop has windows but the registry was stale.
    // Second pass (implicit): eliminate desktops whose windows are NOT on the
    // current desktop. If exactly one desktop remains, it's the empty current one.
    
    struct EnumData {
        IVirtualDesktopManager* pMgr;
        GUID foundId;                 // Set if we directly find a window on current VD
        bool directlyFound;
        std::vector<GUID>* candidates;
    } data = { m_pPublicVirtualDesktopManager.Get(), {}, false, &allDesktops };
    
    EnumWindows([](HWND hw, LPARAM lp) -> BOOL {
        auto* pData = reinterpret_cast<EnumData*>(lp);
        if (!IsWindowVisible(hw)) return TRUE;
        
        GUID desktopId = {};
        if (FAILED(pData->pMgr->GetWindowDesktopId(hw, &desktopId)) ||
            IsEqualGUID(desktopId, GUID{})) {
            return TRUE;  // Skip pinned / all-desktop windows
        }
        
        BOOL onCurrent = FALSE;
        if (SUCCEEDED(pData->pMgr->IsWindowOnCurrentVirtualDesktop(hw, &onCurrent))) {
            if (onCurrent) {
                // Direct hit — this window is on the current desktop
                pData->foundId = desktopId;
                pData->directlyFound = true;
                return FALSE;  // Stop enumeration
            } else {
                // Not on current — remove this desktop from candidates
                auto& c = *pData->candidates;
                c.erase(
                    std::remove_if(c.begin(), c.end(),
                        [&](const GUID& g) { return IsEqualGUID(g, desktopId); }),
                    c.end()
                );
            }
        }
        
        return TRUE;
    }, reinterpret_cast<LPARAM>(&data));
    
    if (data.directlyFound) {
        LOG_DEBUG("FindCurrentDesktopByElimination: direct match");
        return data.foundId;
    }
    
    // Process of elimination
    if (allDesktops.size() == 1) {
        wchar_t guidStr[64] = {};
        StringFromGUID2(allDesktops[0], guidStr, 64);
        LOG_DEBUG("FindCurrentDesktopByElimination: single candidate %ws", guidStr);
        return allDesktops[0];
    }
    
    LOG_DEBUG("FindCurrentDesktopByElimination: %zu candidates remain, cannot determine",
              allDesktops.size());
    return {};
}

void VirtualDesktop::UpdateTrackedForegroundWindow(const GUID& desktopId) {
    if (!m_pPublicVirtualDesktopManager) {
        return;
    }
    
    HWND hFg = GetForegroundWindow();
    if (hFg && IsWindow(hFg)) {
        GUID fgDesktopId = {};
        if (SUCCEEDED(m_pPublicVirtualDesktopManager->GetWindowDesktopId(hFg, &fgDesktopId)) &&
            IsEqualGUID(fgDesktopId, desktopId)) {
            m_lastKnownForegroundHwnd = hFg;
            return;
        }
        // Foreground window might be pinned; check IsWindowOnCurrentVirtualDesktop
        BOOL onCurrent = FALSE;
        if (SUCCEEDED(m_pPublicVirtualDesktopManager->IsWindowOnCurrentVirtualDesktop(hFg, &onCurrent)) 
            && onCurrent && !IsEqualGUID(fgDesktopId, GUID{})) {
            m_lastKnownForegroundHwnd = hFg;
            return;
        }
    }
    
    // Foreground window not usable — find any window on this desktop
    struct EnumData {
        IVirtualDesktopManager* pMgr;
        GUID targetId;
        HWND foundHwnd;
    } data = { m_pPublicVirtualDesktopManager.Get(), desktopId, nullptr };
    
    EnumWindows([](HWND hw, LPARAM lp) -> BOOL {
        auto* pData = reinterpret_cast<EnumData*>(lp);
        if (!IsWindowVisible(hw)) return TRUE;
        
        GUID id = {};
        if (SUCCEEDED(pData->pMgr->GetWindowDesktopId(hw, &id)) &&
            IsEqualGUID(id, pData->targetId)) {
            pData->foundHwnd = hw;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&data));
    
    // If no window was found (empty desktop), keep the previous tracked window.
    // This allows stale-registry detection to keep working on subsequent switches.
    if (!data.foundHwnd && m_lastKnownForegroundHwnd && IsWindow(m_lastKnownForegroundHwnd)) {
        LOG_DEBUG("Empty desktop: keeping previously tracked window %p", m_lastKnownForegroundHwnd);
    } else {
        m_lastKnownForegroundHwnd = data.foundHwnd;
    }
}

void VirtualDesktop::SetDesktopSwitchCallback(DesktopSwitchCallback callback) {
    m_switchCallback = std::move(callback);
    
    // Initialize current desktop ID from registry (most reliable)
    if (!GetCurrentDesktopIdFromRegistry(m_lastKnownDesktopId)) {
        // Fallback to window-based detection
        HWND hForeground = GetForegroundWindow();
        if (hForeground && m_pPublicVirtualDesktopManager) {
            m_pPublicVirtualDesktopManager->GetWindowDesktopId(hForeground, &m_lastKnownDesktopId);
        }
    }
    
    if (!IsEqualGUID(m_lastKnownDesktopId, GUID{})) {
        m_lastKnownDesktopIndex = GetDesktopIndexFromPolling(m_lastKnownDesktopId);
        LOG_INFO("Initial desktop: index=%d", m_lastKnownDesktopIndex);
        UpdateTrackedForegroundWindow(m_lastKnownDesktopId);
        LOG_INFO("Tracked foreground window: %p", m_lastKnownForegroundHwnd);
    } else {
        m_lastKnownDesktopIndex = 1;
        LOG_WARN("Could not determine initial desktop ID");
    }
    
    // Note: App manages the polling timer via TIMER_DESKTOP_POLL
    LOG_INFO("Desktop switch callback registered (using App-managed polling)");
}

void VirtualDesktop::ClearDesktopSwitchCallback() {
    m_switchCallback = nullptr;
    
    if (m_desktopSwitchHook) {
        UnhookWinEvent(m_desktopSwitchHook);
        m_desktopSwitchHook = nullptr;
    }
}

void CALLBACK VirtualDesktop::WinEventProc(HWINEVENTHOOK, DWORD event, HWND, LONG, LONG, DWORD, DWORD) {
    if (event == 0x0020) {
        VirtualDesktop& vd = VirtualDesktop::Instance();
        vd.m_lastKnownDesktopIndex = (vd.m_lastKnownDesktopIndex % 10) + 1;
        vd.OnDesktopSwitched();
    }
}

void VirtualDesktop::CheckDesktopChange() {
    if (!m_switchCallback) {
        return;
    }

    // Re-acquire COM interfaces if they weren't available at startup (boot-time)
    if (m_needsReinit) {
        TryReinitialize();
    }
    
    // Periodic log to confirm polling is active (every ~30 seconds)
    static int pollCount = 0;
    pollCount++;
    if (pollCount % 200 == 1) {  // Every 200 * 150ms = 30s
        wchar_t guidStr[64] = {};
        StringFromGUID2(m_lastKnownDesktopId, guidStr, 64);
        LOG_INFO("Desktop poll #%d - last known: index=%d guid=%ws tracked=%p", 
                 pollCount, m_lastKnownDesktopIndex, guidStr, m_lastKnownForegroundHwnd);
    }
    
    GUID currentDesktopId = {};
    bool registryWorked = false;
    
    // Method 1: Use registry (most reliable, doesn't require windows on desktop)
    if (GetCurrentDesktopIdFromRegistry(currentDesktopId)) {
        registryWorked = true;
    }
    // Method 2: Fallback to IVirtualDesktopManager API
    else if (m_pPublicVirtualDesktopManager) {
        // Get any visible window to check its desktop ID
        HWND hWnd = GetForegroundWindow();
        if (!hWnd) {
            hWnd = GetShellWindow();
        }
        
        if (hWnd) {
            HRESULT hr = m_pPublicVirtualDesktopManager->GetWindowDesktopId(hWnd, &currentDesktopId);
            if (FAILED(hr) || IsEqualGUID(currentDesktopId, GUID{})) {
                // Window might be on all desktops - enumerate to find one
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
                }
            }
        }
    }
    
    // Method 3: Verify registry via IsWindowOnCurrentVirtualDesktop
    // The registry CurrentVirtualDesktop value is not always updated when switching
    // to a desktop with no windows. Cross-check using a tracked window from a
    // previous desktop: if that window's visibility status contradicts the registry,
    // the registry is stale and we must identify the real desktop via elimination.
    if (m_pPublicVirtualDesktopManager && m_lastKnownForegroundHwnd && IsWindow(m_lastKnownForegroundHwnd)) {
        BOOL trackedIsOnCurrent = TRUE;
        bool trackedCheckOK = SUCCEEDED(m_pPublicVirtualDesktopManager->IsWindowOnCurrentVirtualDesktop(
            m_lastKnownForegroundHwnd, &trackedIsOnCurrent));
        
        if (trackedCheckOK) {
            bool needsElimination = false;
            
            if (IsEqualGUID(currentDesktopId, m_lastKnownDesktopId) && !trackedIsOnCurrent) {
                // Registry says same desktop, but tracked window is gone → we moved
                LOG_INFO("Registry stale: tracked window not on current VD, running elimination");
                needsElimination = true;
            } else if (IsEqualGUID(currentDesktopId, GUID{}) && !trackedIsOnCurrent) {
                // No desktop ID from registry/windows, but tracked window moved
                LOG_INFO("No desktop ID but tracked window moved, running elimination");
                needsElimination = true;
            } else if (!IsEqualGUID(currentDesktopId, m_lastKnownDesktopId) 
                       && !IsEqualGUID(currentDesktopId, GUID{})) {
                // Registry says we moved to a different desktop — verify it
                // The tracked window being on current VD means we're on its desktop
                if (trackedIsOnCurrent) {
                    // Tracked window IS on current — trust it, but the registry GUID
                    // might not match. Get the tracked window's actual desktop ID.
                    GUID trackedDesktopId = {};
                    m_pPublicVirtualDesktopManager->GetWindowDesktopId(
                        m_lastKnownForegroundHwnd, &trackedDesktopId);
                    if (!IsEqualGUID(trackedDesktopId, GUID{})) {
                        currentDesktopId = trackedDesktopId;
                    }
                } else {
                    // Tracked window NOT on current, and registry says different desktop
                    // from lastKnown — verify the registry's claim via elimination
                    LOG_INFO("Registry says different desktop but tracked disagrees, verifying");
                    needsElimination = true;
                }
            }
            
            if (needsElimination) {
                GUID eliminatedId = FindCurrentDesktopByElimination();
                if (!IsEqualGUID(eliminatedId, GUID{})) {
                    currentDesktopId = eliminatedId;
                }
            }
        }
    }
    
    // If we couldn't determine current desktop, skip this check
    if (IsEqualGUID(currentDesktopId, GUID{})) {
        if (pollCount % 200 == 1) {
            LOG_WARN("Poll #%d: could not determine current desktop ID", pollCount);
        }
        return;
    }
    
    if (!IsEqualGUID(currentDesktopId, m_lastKnownDesktopId)) {
        wchar_t oldGuid[64] = {}, newGuid[64] = {};
        StringFromGUID2(m_lastKnownDesktopId, oldGuid, 64);
        StringFromGUID2(currentDesktopId, newGuid, 64);
        
        m_lastKnownDesktopId = currentDesktopId;
        
        // Calculate actual desktop index by enumerating desktops
        m_lastKnownDesktopIndex = GetDesktopIndexFromPolling(currentDesktopId);
        m_lastKnownDesktopName = GetDesktopNameFromRegistry(currentDesktopId);
        
        // Update the tracked window for the new desktop
        UpdateTrackedForegroundWindow(currentDesktopId);
        
        LOG_INFO("Desktop change detected! old=%ws new=%ws index=%d tracked=%p", 
                 oldGuid, newGuid, m_lastKnownDesktopIndex, m_lastKnownForegroundHwnd);
        OnDesktopSwitched();
    } else {
        // Same desktop — check if the name was changed (e.g. user renamed it)
        std::wstring currentName = GetDesktopNameFromRegistry(currentDesktopId);
        if (currentName != m_lastKnownDesktopName) {
            m_lastKnownDesktopName = currentName;
            LOG_INFO("Desktop name change detected: %ws", currentName.c_str());
            OnDesktopSwitched();
        }
    }
}

void VirtualDesktop::OnDesktopSwitched() {
    if (!m_switchCallback) {
        return;
    }

    // Use the already-computed cached values from CheckDesktopChange rather than
    // re-querying GetCurrentDesktop, which may read stale registry data when the
    // target desktop has no windows.
    LOG_DEBUG("Desktop switched to: %d (%ws)", m_lastKnownDesktopIndex, m_lastKnownDesktopName.c_str());
    m_switchCallback(m_lastKnownDesktopIndex, m_lastKnownDesktopName);
}

bool VirtualDesktop::MoveWindowToCurrentDesktop(HWND hwnd) {
    if (!hwnd || !m_pPublicVirtualDesktopManager) {
        return false;
    }

    // Check if the window is already on the current virtual desktop
    BOOL isOnCurrent = FALSE;
    HRESULT hr = m_pPublicVirtualDesktopManager->IsWindowOnCurrentVirtualDesktop(hwnd, &isOnCurrent);
    if (SUCCEEDED(hr) && isOnCurrent) {
        return true;  // Already on current desktop
    }

    // Get current desktop ID and move the window there
    GUID currentId = m_lastKnownDesktopId;
    if (IsEqualGUID(currentId, GUID{})) {
        GetCurrentDesktopIdFromRegistry(currentId);
    }

    if (!IsEqualGUID(currentId, GUID{})) {
        hr = m_pPublicVirtualDesktopManager->MoveWindowToDesktop(hwnd, currentId);
        if (SUCCEEDED(hr)) {
            LOG_DEBUG("Moved window %p to current desktop", hwnd);
            return true;
        }
        LOG_WARN("MoveWindowToDesktop failed: 0x%08X", hr);
    }
    return false;
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
