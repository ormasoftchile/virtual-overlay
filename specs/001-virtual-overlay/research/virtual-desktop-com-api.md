# Windows Virtual Desktop COM API Research

## Decision: Recommended Approach

**Use a hybrid approach combining the documented `IVirtualDesktopManager` for basic operations with undocumented internal interfaces for extended functionality, wrapped with runtime version detection and graceful fallbacks.**

## Rationale

1. **Documented API is limited**: `IVirtualDesktopManager` only provides:
   - `IsWindowOnCurrentVirtualDesktop(HWND)` - check if window is on current desktop
   - `GetWindowDesktopId(HWND)` - get desktop GUID for a window  
   - `MoveWindowToDesktop(HWND, GUID)` - move window to desktop

2. **Critical features require undocumented interfaces**:
   - Getting desktop names requires `IVirtualDesktop::GetName()`
   - Desktop change notifications require `IVirtualDesktopNotification`
   - Switching desktops requires `IVirtualDesktopManagerInternal::SwitchDesktop()`
   - Getting current desktop requires `IVirtualDesktopManagerInternal::GetCurrentDesktop()`

3. **Interface GUIDs change between Windows versions** - Microsoft updates these with major Windows releases, requiring version-specific implementations.

---

## 1. Documented Interface: IVirtualDesktopManager

**Header**: `<shobjidl_core.h>`  
**CLSID**: `AA509086-5CA9-4C25-8F95-589D3C07B48A`  
**IID**: `A5CD92FF-29BE-454C-8D04-D82879FB3F1B`

### Available Methods
```cpp
interface IVirtualDesktopManager : IUnknown {
    HRESULT IsWindowOnCurrentVirtualDesktop(HWND topLevelWindow, BOOL* onCurrentDesktop);
    HRESULT GetWindowDesktopId(HWND topLevelWindow, GUID* desktopId);
    HRESULT MoveWindowToDesktop(HWND topLevelWindow, REFGUID desktopId);
};
```

### Limitations
- **Cannot get current desktop** - no method to retrieve which desktop is active
- **Cannot get desktop name** - only returns GUIDs, not user-visible names
- **Cannot enumerate desktops** - no way to list all available desktops
- **Cannot switch desktops** - only moves windows, not user focus
- **No notifications** - cannot detect when user switches desktops

---

## 2. Undocumented Interfaces

### Key Interfaces Required

| Interface | Purpose |
|-----------|---------|
| `IVirtualDesktop` | Represents a single desktop; provides `GetId()` and `GetName()` |
| `IVirtualDesktopManagerInternal` | Core operations: enumerate, switch, create, remove desktops |
| `IVirtualDesktopNotification` | Callback interface for desktop change events |
| `IVirtualDesktopNotificationService` | Register/unregister notification callbacks |
| `IApplicationViewCollection` | Get application views for window-to-desktop operations |
| `IVirtualDesktopPinnedApps` | Pin windows/apps to all desktops |

### Service Provider Pattern
All internal interfaces are obtained via `IServiceProvider` from the Immersive Shell:

```cpp
// CLSID_ImmersiveShell
const GUID CLSID_ImmersiveShell = {0xC2F03A33, 0x21F5, 0x47FA, {0xB4, 0xBB, 0x15, 0x63, 0x62, 0xA2, 0xF2, 0x39}};

// Get shell service provider
IServiceProvider* pServiceProvider = nullptr;
CoCreateInstance(CLSID_ImmersiveShell, nullptr, CLSCTX_LOCAL_SERVER, 
                 IID_PPV_ARGS(&pServiceProvider));

// Query for internal interface
IVirtualDesktopManagerInternal* pDesktopManagerInternal = nullptr;
pServiceProvider->QueryService(CLSID_VirtualDesktopManagerInternal,
                               IID_IVirtualDesktopManagerInternal,
                               (void**)&pDesktopManagerInternal);
```

---

## 3. Getting Desktop Names (IVirtualDesktop::GetName)

The `IVirtualDesktop` interface provides:

```cpp
interface IVirtualDesktop : IUnknown {
    HRESULT IsViewVisible(IApplicationView* pView, BOOL* pfVisible);
    HRESULT GetId(GUID* pGuid);
    HRESULT GetMonitor(HMONITOR* pMonitor);
    HRESULT GetName(HSTRING* pName);  // Returns desktop name (Win11+)
    HRESULT GetWallpaper(HSTRING* pPath);  // Returns wallpaper path (Win11+)
};
```

### Usage Pattern
```cpp
IVirtualDesktop* pDesktop = nullptr;
pDesktopManagerInternal->GetCurrentDesktop(&pDesktop);

HSTRING name;
if (SUCCEEDED(pDesktop->GetName(&name))) {
    PCWSTR pszName = WindowsGetStringRawBuffer(name, nullptr);
    // Use pszName - may be empty if user hasn't renamed desktop
    WindowsDeleteString(name);
}

// If name is empty, generate "Desktop N" based on index
if (wcslen(pszName) == 0) {
    swprintf_s(buffer, L"Desktop %d", desktopIndex + 1);
}
```

---

## 4. Desktop Change Notifications (IVirtualDesktopNotification)

### Notification Interface
```cpp
interface IVirtualDesktopNotification : IUnknown {
    HRESULT VirtualDesktopCreated(IVirtualDesktop* pDesktop);
    HRESULT VirtualDesktopDestroyBegin(IVirtualDesktop* pDesktopDestroyed, 
                                       IVirtualDesktop* pDesktopFallback);
    HRESULT VirtualDesktopDestroyFailed(IVirtualDesktop* pDesktopDestroyed,
                                        IVirtualDesktop* pDesktopFallback);
    HRESULT VirtualDesktopDestroyed(IVirtualDesktop* pDesktopDestroyed,
                                    IVirtualDesktop* pDesktopFallback);
    HRESULT VirtualDesktopMoved(IVirtualDesktop* pDesktop, 
                                INT64 nOldIndex, INT64 nNewIndex);
    HRESULT VirtualDesktopNameChanged(IVirtualDesktop* pDesktop, HSTRING name);
    HRESULT ViewVirtualDesktopChanged(IApplicationView* pView);
    HRESULT CurrentVirtualDesktopChanged(IVirtualDesktop* pDesktopOld,
                                         IVirtualDesktop* pDesktopNew);
    HRESULT VirtualDesktopWallpaperChanged(IVirtualDesktop* pDesktop, HSTRING path);
    HRESULT VirtualDesktopSwitched(IVirtualDesktop* pDesktop);
    HRESULT RemoteVirtualDesktopConnected(IVirtualDesktop* pDesktop);
};
```

### Registration
```cpp
class DesktopNotificationListener : public IVirtualDesktopNotification {
    // Implement IUnknown + all notification methods
    
    HRESULT CurrentVirtualDesktopChanged(IVirtualDesktop* pOld, IVirtualDesktop* pNew) override {
        // Handle desktop switch - this is the key event
        GUID newId;
        pNew->GetId(&newId);
        
        HSTRING name;
        pNew->GetName(&name);
        
        // Display notification to user
        return S_OK;
    }
};

// Register for notifications
IVirtualDesktopNotificationService* pNotificationService = nullptr;
pServiceProvider->QueryService(CLSID_VirtualDesktopNotificationService,
                               IID_IVirtualDesktopNotificationService,
                               (void**)&pNotificationService);

DWORD dwCookie;
auto pListener = new DesktopNotificationListener();
pNotificationService->Register(pListener, &dwCookie);

// Later: Unregister
pNotificationService->Unregister(dwCookie);
```

---

## 5. Version-Specific GUIDs

**Critical**: These interface GUIDs change between Windows versions. The CLSID values for service lookup are generally stable, but the interface IIDs vary.

### Interface GUID Table

| Interface | Windows 10 (build 19041+) | Windows 11 21H2/22H2 | Windows 11 23H2/24H2 |
|-----------|--------------------------|---------------------|---------------------|
| `IVirtualDesktop` | `FF72FFDD-BE7E-43FC-9C03-AD81681E88E4` | `536D3495-B208-4CC9-AE26-DE8111275BF8` | `3F07F4BE-B107-441A-AF0F-39D82529072C` |
| `IVirtualDesktopManagerInternal` | `F31574D6-B682-4CDC-BD56-1827860ABEC6` | `B2F925B9-5A0F-4D2E-9F4D-2B1507593C10` | `53F5CA0B-158F-4124-900C-057158060B27` |
| `IVirtualDesktopNotification` | `C179334C-4295-40D3-BEA1-C654D965605A` | `CD403E52-DEED-4C13-B437-B98380F2B1E8` | `B9E5E94D-233E-49AB-AF5C-2B4541C3AADE` |
| `IVirtualDesktopNotificationService` | `0CD45E71-D927-4F15-8B0A-8FEF525337BF` | `0CD45E71-D927-4F15-8B0A-8FEF525337BF` | `0CD45E71-D927-4F15-8B0A-8FEF525337BF` |

### Stable CLSIDs (Service Lookup)
```cpp
// These CLSIDs are stable across versions
const GUID CLSID_ImmersiveShell = 
    {0xC2F03A33, 0x21F5, 0x47FA, {0xB4, 0xBB, 0x15, 0x63, 0x62, 0xA2, 0xF2, 0x39}};
    
const GUID CLSID_VirtualDesktopManager = 
    {0xAA509086, 0x5CA9, 0x4C25, {0x8F, 0x95, 0x58, 0x9D, 0x3C, 0x07, 0xB4, 0x8A}};
    
const GUID CLSID_VirtualDesktopManagerInternal = 
    {0xC5E0CDCA, 0x7B6E, 0x41B2, {0x9F, 0xC4, 0xD9, 0x39, 0x75, 0xCC, 0x46, 0x7B}};
    
const GUID CLSID_VirtualDesktopPinnedApps = 
    {0xB5A399E7, 0x1C87, 0x46B8, {0x88, 0xE9, 0xFC, 0x57, 0x47, 0xB1, 0x71, 0xBD}};
    
const GUID CLSID_VirtualDesktopNotificationService = 
    {0xA501FDEC, 0x4A09, 0x464C, {0xAE, 0x4E, 0x1B, 0x9C, 0x21, 0xB8, 0x49, 0x18}};
```

---

## 6. Best Practices for Version Handling

### Runtime Version Detection
```cpp
#include <VersionHelpers.h>
#include <ntverp.h>

struct WindowsVersion {
    DWORD major;
    DWORD minor;
    DWORD build;
};

WindowsVersion GetWindowsVersion() {
    // Use RtlGetVersion for accurate version info
    typedef NTSTATUS(WINAPI* RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
    
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    auto RtlGetVersion = (RtlGetVersionPtr)GetProcAddress(hNtdll, "RtlGetVersion");
    
    RTL_OSVERSIONINFOW osvi = { sizeof(osvi) };
    RtlGetVersion(&osvi);
    
    return { osvi.dwMajorVersion, osvi.dwMinorVersion, osvi.dwBuildNumber };
}

const GUID& GetVirtualDesktopIID() {
    static WindowsVersion ver = GetWindowsVersion();
    
    // Windows 11 24H2+ (build 26100+)
    if (ver.build >= 26100) {
        return IID_IVirtualDesktop_Win11_24H2;
    }
    // Windows 11 23H2 (build 22621.3085+)
    else if (ver.build >= 22621) {
        return IID_IVirtualDesktop_Win11_23H2;
    }
    // Windows 11 21H2/22H2
    else if (ver.build >= 22000) {
        return IID_IVirtualDesktop_Win11;
    }
    // Windows 10
    else {
        return IID_IVirtualDesktop_Win10;
    }
}
```

### Interface Wrapper with Version Support
```cpp
class VirtualDesktopAPI {
    IServiceProvider* m_pServiceProvider = nullptr;
    IVirtualDesktopManagerInternal* m_pManagerInternal = nullptr;
    IVirtualDesktopNotificationService* m_pNotificationService = nullptr;
    DWORD m_dwNotificationCookie = 0;
    
public:
    HRESULT Initialize() {
        HRESULT hr = CoCreateInstance(CLSID_ImmersiveShell, nullptr, 
                                      CLSCTX_LOCAL_SERVER,
                                      IID_PPV_ARGS(&m_pServiceProvider));
        if (FAILED(hr)) return hr;
        
        // Try version-specific interfaces
        hr = m_pServiceProvider->QueryService(
            CLSID_VirtualDesktopManagerInternal,
            GetVirtualDesktopManagerInternalIID(),
            (void**)&m_pManagerInternal);
            
        if (FAILED(hr)) {
            // Fallback: try older interface versions
            hr = TryFallbackInterfaces();
        }
        
        return hr;
    }
    
    HRESULT GetCurrentDesktopName(std::wstring& name) {
        IVirtualDesktop* pDesktop = nullptr;
        HRESULT hr = m_pManagerInternal->GetCurrentDesktop(&pDesktop);
        if (FAILED(hr)) return hr;
        
        HSTRING hName;
        hr = pDesktop->GetName(&hName);
        if (SUCCEEDED(hr)) {
            PCWSTR pszName = WindowsGetStringRawBuffer(hName, nullptr);
            name = pszName ? pszName : L"";
            WindowsDeleteString(hName);
        }
        
        pDesktop->Release();
        return hr;
    }
    
    HRESULT GetCurrentDesktopIndex(int& index) {
        IVirtualDesktop* pCurrent = nullptr;
        HRESULT hr = m_pManagerInternal->GetCurrentDesktop(&pCurrent);
        if (FAILED(hr)) return hr;
        
        GUID currentId;
        pCurrent->GetId(&currentId);
        
        IObjectArray* pDesktops = nullptr;
        hr = m_pManagerInternal->GetDesktops(&pDesktops);
        if (SUCCEEDED(hr)) {
            UINT count;
            pDesktops->GetCount(&count);
            
            for (UINT i = 0; i < count; i++) {
                IVirtualDesktop* pDesktop = nullptr;
                pDesktops->GetAt(i, GetVirtualDesktopIID(), (void**)&pDesktop);
                
                GUID id;
                pDesktop->GetId(&id);
                
                if (IsEqualGUID(id, currentId)) {
                    index = (int)i;
                    pDesktop->Release();
                    break;
                }
                pDesktop->Release();
            }
            pDesktops->Release();
        }
        
        pCurrent->Release();
        return hr;
    }
};
```

---

## 7. Fallback Strategies

### When Undocumented Interfaces Fail

1. **Polling-based detection**: Use `IVirtualDesktopManager::GetWindowDesktopId()` on a hidden helper window to detect when the current desktop GUID changes.

2. **Registry monitoring**: Desktop names are stored in the registry at:
   ```
   HKEY_CURRENT_USER\SOFTWARE\Microsoft\Windows\CurrentVersion\Explorer\VirtualDesktops\Desktops
   ```
   Each desktop has a subkey with its GUID containing `Name` and `Wallpaper` values.

3. **Desktop count from registry**:
   ```
   HKEY_CURRENT_USER\SOFTWARE\Microsoft\Windows\CurrentVersion\Explorer\VirtualDesktops
   Value: VirtualDesktopCount (DWORD)
   ```

### Polling Implementation
```cpp
class FallbackDesktopMonitor {
    IVirtualDesktopManager* m_pManager = nullptr;
    HWND m_hHelperWindow = nullptr;
    GUID m_lastDesktopId = {};
    std::thread m_pollThread;
    std::atomic<bool> m_running = true;
    
public:
    void Start(std::function<void(GUID)> onDesktopChanged) {
        // Create invisible helper window
        m_hHelperWindow = CreateWindowExW(0, L"STATIC", L"", 
                                          0, 0, 0, 0, 0, 
                                          HWND_MESSAGE, nullptr, nullptr, nullptr);
        
        CoCreateInstance(CLSID_VirtualDesktopManager, nullptr, 
                        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_pManager));
        
        m_pManager->GetWindowDesktopId(m_hHelperWindow, &m_lastDesktopId);
        
        m_pollThread = std::thread([this, onDesktopChanged]() {
            while (m_running) {
                GUID currentId;
                if (SUCCEEDED(m_pManager->GetWindowDesktopId(m_hHelperWindow, &currentId))) {
                    if (!IsEqualGUID(currentId, m_lastDesktopId)) {
                        m_lastDesktopId = currentId;
                        onDesktopChanged(currentId);
                    }
                }
                Sleep(100);  // Poll every 100ms
            }
        });
    }
};
```

### Registry-based Desktop Name Lookup
```cpp
std::wstring GetDesktopNameFromRegistry(const GUID& desktopId) {
    wchar_t guidStr[40];
    StringFromGUID2(desktopId, guidStr, 40);
    
    std::wstring keyPath = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VirtualDesktops\\Desktops\\";
    keyPath += guidStr;
    
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, keyPath.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t name[256];
        DWORD size = sizeof(name);
        DWORD type;
        
        if (RegQueryValueExW(hKey, L"Name", nullptr, &type, (LPBYTE)name, &size) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return name;
        }
        RegCloseKey(hKey);
    }
    
    return L"";  // Name not set
}
```

---

## 8. Alternatives Considered

### Alternative 1: Use PowerShell/External Process
- Execute `VirtualDesktop.exe` or PowerShell cmdlets as child process
- **Rejected**: High latency, process creation overhead, not suitable for real-time notifications

### Alternative 2: UI Automation
- Use Windows UI Automation to detect Task View changes
- **Rejected**: Unreliable, depends on UI state, doesn't work when Task View isn't visible

### Alternative 3: Hook Explorer.exe
- Inject into explorer.exe to access internal Shell32 virtual desktop functions
- **Rejected**: Invasive, security concerns, fragile across updates, may trigger antivirus

### Alternative 4: Pure Registry Monitoring
- Only use registry values, no COM interfaces
- **Rejected**: No reliable way to detect desktop switches; registry doesn't update immediately

### Alternative 5: Use Existing Library
- Link against Ciantic/VirtualDesktopAccessor DLL
- **Considered**: Good option if acceptable to ship external DLL; provides stable API with version handling built-in. The DLL is ~200KB and MIT licensed.

---

## Code Snippet: Complete Initialization

```cpp
#include <windows.h>
#include <initguid.h>
#include <objbase.h>
#include <shobjidl_core.h>
#include <winstring.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "runtimeobject.lib")

// Stable CLSIDs
DEFINE_GUID(CLSID_ImmersiveShell,
    0xC2F03A33, 0x21F5, 0x47FA, 0xB4, 0xBB, 0x15, 0x63, 0x62, 0xA2, 0xF2, 0x39);
DEFINE_GUID(CLSID_VirtualDesktopManagerInternal,
    0xC5E0CDCA, 0x7B6E, 0x41B2, 0x9F, 0xC4, 0xD9, 0x39, 0x75, 0xCC, 0x46, 0x7B);
DEFINE_GUID(CLSID_VirtualDesktopNotificationService,
    0xA501FDEC, 0x4A09, 0x464C, 0xAE, 0x4E, 0x1B, 0x9C, 0x21, 0xB8, 0x49, 0x18);

// Windows 11 24H2 IIDs (update for other versions)
DEFINE_GUID(IID_IVirtualDesktop,
    0x3F07F4BE, 0xB107, 0x441A, 0xAF, 0x0F, 0x39, 0xD8, 0x25, 0x29, 0x07, 0x2C);
DEFINE_GUID(IID_IVirtualDesktopManagerInternal,
    0x53F5CA0B, 0x158F, 0x4124, 0x90, 0x0C, 0x05, 0x71, 0x58, 0x06, 0x0B, 0x27);
DEFINE_GUID(IID_IVirtualDesktopNotification,
    0xB9E5E94D, 0x233E, 0x49AB, 0xAF, 0x5C, 0x2B, 0x45, 0x41, 0xC3, 0xAA, 0xDE);
DEFINE_GUID(IID_IVirtualDesktopNotificationService,
    0x0CD45E71, 0xD927, 0x4F15, 0x8B, 0x0A, 0x8F, 0xEF, 0x52, 0x53, 0x37, 0xBF);

// Forward declare interfaces (define vtables based on version)
MIDL_INTERFACE("3F07F4BE-B107-441A-AF0F-39D82529072C")
IVirtualDesktop : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE IsViewVisible(
        IUnknown* pView, BOOL* pfVisible) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetId(GUID* pGuid) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMonitor(HMONITOR* pMonitor) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetName(HSTRING* pName) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetWallpaper(HSTRING* pPath) = 0;
};

bool InitializeVirtualDesktopAPI() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        return false;
    }
    
    IServiceProvider* pServiceProvider = nullptr;
    hr = CoCreateInstance(CLSID_ImmersiveShell, nullptr, CLSCTX_LOCAL_SERVER,
                          IID_IServiceProvider, (void**)&pServiceProvider);
    if (FAILED(hr)) {
        return false;
    }
    
    IVirtualDesktopManagerInternal* pDesktopManager = nullptr;
    hr = pServiceProvider->QueryService(CLSID_VirtualDesktopManagerInternal,
                                        IID_IVirtualDesktopManagerInternal,
                                        (void**)&pDesktopManager);
    if (FAILED(hr)) {
        pServiceProvider->Release();
        return false;
    }
    
    // Success - store pointers for later use
    // ...
    
    return true;
}
```

---

## Summary

| Requirement | API | Availability |
|-------------|-----|--------------|
| Check if window on current desktop | `IVirtualDesktopManager` | Documented, stable |
| Get desktop GUID for window | `IVirtualDesktopManager` | Documented, stable |
| Move window to desktop | `IVirtualDesktopManager` | Documented, stable |
| Get current desktop | `IVirtualDesktopManagerInternal` | Undocumented, version-specific |
| Get desktop name | `IVirtualDesktop::GetName` | Undocumented, version-specific |
| Desktop count | `IVirtualDesktopManagerInternal` | Undocumented, version-specific |
| Desktop switch notification | `IVirtualDesktopNotification` | Undocumented, version-specific |
| Switch desktop programmatically | `IVirtualDesktopManagerInternal` | Undocumented, version-specific |

**Recommendation**: Start with documented API for basic functionality, add undocumented interfaces wrapped with version detection for full feature set. Include polling fallback if interfaces fail to load.
