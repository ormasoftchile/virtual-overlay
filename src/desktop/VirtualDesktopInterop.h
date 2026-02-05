#pragma once

// VirtualDesktopInterop.h - Undocumented COM interfaces for Windows Virtual Desktops
// These interfaces are reverse-engineered and may change between Windows versions.
// GUIDs are specific to Windows build numbers.

#include <Windows.h>
#include <objbase.h>
#include <inspectable.h>
#include <ObjectArray.h>
#include <winternl.h>

namespace VirtualOverlay {

// =============================================================================
// Common GUIDs (stable across versions)
// =============================================================================

// CLSID for the Virtual Desktop Manager (public, documented)
EXTERN_C const CLSID CLSID_VirtualDesktopManager;

// IID for IVirtualDesktopManager (public, documented)
// {a5cd92ff-29be-454c-8d04-d82879fb3f1b}
EXTERN_C const IID IID_IVirtualDesktopManager;

// CLSID for the immersive shell Virtual Desktop Notification Service
// {c5e0cdca-7b6e-41b2-9fc4-d93975cc467b}
EXTERN_C const CLSID CLSID_VirtualDesktopNotificationService;

// =============================================================================
// Windows Version Detection
// =============================================================================

enum class WindowsVirtualDesktopVersion {
    Unknown = 0,
    Win10_1803,      // Build 17134
    Win10_1809,      // Build 17763
    Win10_1903,      // Build 18362
    Win10_1909,      // Build 18363
    Win10_2004,      // Build 19041
    Win10_20H2,      // Build 19042
    Win10_21H1,      // Build 19043
    Win10_21H2,      // Build 19044
    Win10_22H2,      // Build 19045
    Win11_21H2,      // Build 22000
    Win11_22H2,      // Build 22621
    Win11_23H2,      // Build 22631
    Win11_24H2,      // Build 26100
    Win11_24H2_Preview, // Build 26200+ (Insider/Preview builds)
};

inline WindowsVirtualDesktopVersion GetCurrentVirtualDesktopVersion() {
    RTL_OSVERSIONINFOW osvi = {};
    osvi.dwOSVersionInfoSize = sizeof(osvi);
    
    // Use RtlGetVersion to bypass compatibility shims
    typedef LONG(WINAPI* RtlGetVersionFn)(RTL_OSVERSIONINFOW*);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll) {
        auto pRtlGetVersion = reinterpret_cast<RtlGetVersionFn>(
            GetProcAddress(ntdll, "RtlGetVersion"));
        if (pRtlGetVersion) {
            pRtlGetVersion(&osvi);
        }
    }

    DWORD build = osvi.dwBuildNumber;

    // Windows 11 24H2 Preview/Insider builds (26200+)
    if (build >= 26200) return WindowsVirtualDesktopVersion::Win11_24H2_Preview;
    // Windows 11 24H2
    if (build >= 26100) return WindowsVirtualDesktopVersion::Win11_24H2;
    // Windows 11 23H2
    if (build >= 22631) return WindowsVirtualDesktopVersion::Win11_23H2;
    // Windows 11 22H2
    if (build >= 22621) return WindowsVirtualDesktopVersion::Win11_22H2;
    // Windows 11 21H2
    if (build >= 22000) return WindowsVirtualDesktopVersion::Win11_21H2;
    // Windows 10 22H2
    if (build >= 19045) return WindowsVirtualDesktopVersion::Win10_22H2;
    // Windows 10 21H2
    if (build >= 19044) return WindowsVirtualDesktopVersion::Win10_21H2;
    // Windows 10 21H1
    if (build >= 19043) return WindowsVirtualDesktopVersion::Win10_21H1;
    // Windows 10 20H2
    if (build >= 19042) return WindowsVirtualDesktopVersion::Win10_20H2;
    // Windows 10 2004
    if (build >= 19041) return WindowsVirtualDesktopVersion::Win10_2004;
    // Windows 10 1909
    if (build >= 18363) return WindowsVirtualDesktopVersion::Win10_1909;
    // Windows 10 1903
    if (build >= 18362) return WindowsVirtualDesktopVersion::Win10_1903;
    // Windows 10 1809
    if (build >= 17763) return WindowsVirtualDesktopVersion::Win10_1809;
    // Windows 10 1803
    if (build >= 17134) return WindowsVirtualDesktopVersion::Win10_1803;
    
    return WindowsVirtualDesktopVersion::Unknown;
}

// =============================================================================
// Forward Declarations
// =============================================================================

struct IVirtualDesktop;
struct IVirtualDesktopManagerInternal;
struct IVirtualDesktopNotification;
struct IVirtualDesktopNotificationService;
struct IApplicationView;

// =============================================================================
// IVirtualDesktop - Represents a single virtual desktop
// =============================================================================

// Windows 10 (all builds) - {ff72ffdd-be7e-43fc-9c03-ad81681e88e4}
MIDL_INTERFACE("ff72ffdd-be7e-43fc-9c03-ad81681e88e4")
IVirtualDesktop_Win10 : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE IsViewVisible(
        IApplicationView* pView,
        BOOL* pfVisible) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetID(GUID* pGuid) = 0;
};

// Windows 11 21H2/22H2 - {536d3495-b208-4cc9-ae26-de8111275bf8}
MIDL_INTERFACE("536d3495-b208-4cc9-ae26-de8111275bf8")
IVirtualDesktop_Win11_21H2 : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE IsViewVisible(
        IApplicationView* pView,
        BOOL* pfVisible) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetID(GUID* pGuid) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMonitor(HMONITOR* pMonitor) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetName(HSTRING* pName) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetWallpaperPath(HSTRING* pPath) = 0;
};

// Windows 11 23H2+ - {3f07f4be-b107-441a-af0f-39d82529072c}
MIDL_INTERFACE("3f07f4be-b107-441a-af0f-39d82529072c")
IVirtualDesktop_Win11_23H2 : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE IsViewVisible(
        IApplicationView* pView,
        BOOL* pfVisible) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetID(GUID* pGuid) = 0;
    virtual HRESULT STDMETHODCALLTYPE Unknown1() = 0;  // Added in 23H2
    virtual HRESULT STDMETHODCALLTYPE GetName(HSTRING* pName) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetWallpaperPath(HSTRING* pPath) = 0;
};

// Windows 11 24H2 Preview (Build 26200+) - {9f4c7c69-6ed1-408c-a3a9-1c0f89e3b7b2}
MIDL_INTERFACE("9f4c7c69-6ed1-408c-a3a9-1c0f89e3b7b2")
IVirtualDesktop_Win11_24H2_Preview : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE IsViewVisible(
        IApplicationView* pView,
        BOOL* pfVisible) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetID(GUID* pGuid) = 0;
    virtual HRESULT STDMETHODCALLTYPE Unknown1() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetName(HSTRING* pName) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetWallpaperPath(HSTRING* pPath) = 0;
};

// =============================================================================
// IVirtualDesktopManagerInternal - Main interface for desktop management
// =============================================================================

// Windows 10 (builds 17134-19045) - {f31574d6-b682-4cdc-bd56-1827860abec6}
MIDL_INTERFACE("f31574d6-b682-4cdc-bd56-1827860abec6")
IVirtualDesktopManagerInternal_Win10 : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE GetCount(UINT* pCount) = 0;
    virtual HRESULT STDMETHODCALLTYPE MoveViewToDesktop(
        IApplicationView* pView,
        IVirtualDesktop_Win10* pDesktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE CanViewMoveDesktops(
        IApplicationView* pView,
        BOOL* pfCanViewMoveDesktops) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentDesktop(
        IVirtualDesktop_Win10** ppDesktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDesktops(
        IObjectArray** ppDesktops) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetAdjacentDesktop(
        IVirtualDesktop_Win10* pDesktopReference,
        int nDirection,
        IVirtualDesktop_Win10** ppAdjacentDesktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE SwitchDesktop(
        IVirtualDesktop_Win10* pDesktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateDesktop(
        IVirtualDesktop_Win10** ppNewDesktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE RemoveDesktop(
        IVirtualDesktop_Win10* pRemove,
        IVirtualDesktop_Win10* pFallbackDesktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE FindDesktop(
        const GUID* pGuid,
        IVirtualDesktop_Win10** ppDesktop) = 0;
};

// Windows 11 21H2/22H2 - {b2f925b9-5a0f-4d2e-9f4d-2b1507593c10}
MIDL_INTERFACE("b2f925b9-5a0f-4d2e-9f4d-2b1507593c10")
IVirtualDesktopManagerInternal_Win11_21H2 : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE GetCount(HMONITOR hMonitor, UINT* pCount) = 0;
    virtual HRESULT STDMETHODCALLTYPE MoveViewToDesktop(
        IApplicationView* pView,
        IVirtualDesktop_Win11_21H2* pDesktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE CanViewMoveDesktops(
        IApplicationView* pView,
        BOOL* pfCanViewMoveDesktops) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentDesktop(
        HMONITOR hMonitor,
        IVirtualDesktop_Win11_21H2** ppDesktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetAllCurrentDesktops(
        IObjectArray** ppDesktops) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDesktops(
        HMONITOR hMonitor,
        IObjectArray** ppDesktops) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetAdjacentDesktop(
        IVirtualDesktop_Win11_21H2* pDesktopReference,
        int nDirection,
        IVirtualDesktop_Win11_21H2** ppAdjacentDesktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE SwitchDesktop(
        HMONITOR hMonitor,
        IVirtualDesktop_Win11_21H2* pDesktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateDesktop(
        HMONITOR hMonitor,
        IVirtualDesktop_Win11_21H2** ppNewDesktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE MoveDesktop(
        IVirtualDesktop_Win11_21H2* pDesktop,
        HMONITOR hMonitor,
        int nIndex) = 0;
    virtual HRESULT STDMETHODCALLTYPE RemoveDesktop(
        IVirtualDesktop_Win11_21H2* pRemove,
        IVirtualDesktop_Win11_21H2* pFallbackDesktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE FindDesktop(
        const GUID* pGuid,
        IVirtualDesktop_Win11_21H2** ppDesktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDesktopSwitchIncludeExcludeViews(
        IVirtualDesktop_Win11_21H2* pDesktop,
        IObjectArray** ppViews1,
        IObjectArray** ppViews2) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDesktopName(
        IVirtualDesktop_Win11_21H2* pDesktop,
        HSTRING name) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDesktopWallpaper(
        IVirtualDesktop_Win11_21H2* pDesktop,
        HSTRING path) = 0;
    virtual HRESULT STDMETHODCALLTYPE UpdateWallpaperPathForAllDesktops(
        HSTRING path) = 0;
    virtual HRESULT STDMETHODCALLTYPE CopyDesktopState(
        IApplicationView* pView1,
        IApplicationView* pView2) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDesktopIsPerMonitor(
        BOOL* pfPerMonitor) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDesktopIsPerMonitor(
        BOOL fPerMonitor) = 0;
};

// Windows 11 23H2+ - {a3175f2d-239c-4bd2-8aa0-eeba8b0b138e}
MIDL_INTERFACE("a3175f2d-239c-4bd2-8aa0-eeba8b0b138e")
IVirtualDesktopManagerInternal_Win11_23H2 : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE GetCount(HMONITOR hMonitor, UINT* pCount) = 0;
    virtual HRESULT STDMETHODCALLTYPE MoveViewToDesktop(
        IApplicationView* pView,
        IVirtualDesktop_Win11_23H2* pDesktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE CanViewMoveDesktops(
        IApplicationView* pView,
        BOOL* pfCanViewMoveDesktops) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentDesktop(
        HMONITOR hMonitor,
        IVirtualDesktop_Win11_23H2** ppDesktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetAllCurrentDesktops(
        IObjectArray** ppDesktops) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDesktops(
        HMONITOR hMonitor,
        IObjectArray** ppDesktops) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetAdjacentDesktop(
        IVirtualDesktop_Win11_23H2* pDesktopReference,
        int nDirection,
        IVirtualDesktop_Win11_23H2** ppAdjacentDesktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE SwitchDesktop(
        HMONITOR hMonitor,
        IVirtualDesktop_Win11_23H2* pDesktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateDesktop(
        HMONITOR hMonitor,
        IVirtualDesktop_Win11_23H2** ppNewDesktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE MoveDesktop(
        IVirtualDesktop_Win11_23H2* pDesktop,
        HMONITOR hMonitor,
        int nIndex) = 0;
    virtual HRESULT STDMETHODCALLTYPE RemoveDesktop(
        IVirtualDesktop_Win11_23H2* pRemove,
        IVirtualDesktop_Win11_23H2* pFallbackDesktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE FindDesktop(
        const GUID* pGuid,
        IVirtualDesktop_Win11_23H2** ppDesktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDesktopSwitchIncludeExcludeViews(
        IVirtualDesktop_Win11_23H2* pDesktop,
        IObjectArray** ppViews1,
        IObjectArray** ppViews2) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDesktopName(
        IVirtualDesktop_Win11_23H2* pDesktop,
        HSTRING name) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDesktopWallpaper(
        IVirtualDesktop_Win11_23H2* pDesktop,
        HSTRING path) = 0;
    virtual HRESULT STDMETHODCALLTYPE UpdateWallpaperPathForAllDesktops(
        HSTRING path) = 0;
    virtual HRESULT STDMETHODCALLTYPE CopyDesktopState(
        IApplicationView* pView1,
        IApplicationView* pView2) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDesktopIsPerMonitor(
        BOOL* pfPerMonitor) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDesktopIsPerMonitor(
        BOOL fPerMonitor) = 0;
};

// Windows 11 24H2 Preview (Build 26200+) - {53F5CA0B-158F-4124-900C-057158060B27}
MIDL_INTERFACE("53F5CA0B-158F-4124-900C-057158060B27")
IVirtualDesktopManagerInternal_Win11_24H2_Preview : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE GetCount(HMONITOR hMonitor, UINT* pCount) = 0;
    virtual HRESULT STDMETHODCALLTYPE MoveViewToDesktop(
        IApplicationView* pView,
        IVirtualDesktop_Win11_24H2_Preview* pDesktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE CanViewMoveDesktops(
        IApplicationView* pView,
        BOOL* pfCanViewMoveDesktops) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentDesktop(
        HMONITOR hMonitor,
        IVirtualDesktop_Win11_24H2_Preview** ppDesktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetAllCurrentDesktops(
        IObjectArray** ppDesktops) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDesktops(
        HMONITOR hMonitor,
        IObjectArray** ppDesktops) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetAdjacentDesktop(
        IVirtualDesktop_Win11_24H2_Preview* pDesktopReference,
        int nDirection,
        IVirtualDesktop_Win11_24H2_Preview** ppAdjacentDesktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE SwitchDesktop(
        HMONITOR hMonitor,
        IVirtualDesktop_Win11_24H2_Preview* pDesktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateDesktop(
        HMONITOR hMonitor,
        IVirtualDesktop_Win11_24H2_Preview** ppNewDesktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE MoveDesktop(
        IVirtualDesktop_Win11_24H2_Preview* pDesktop,
        HMONITOR hMonitor,
        int nIndex) = 0;
    virtual HRESULT STDMETHODCALLTYPE RemoveDesktop(
        IVirtualDesktop_Win11_24H2_Preview* pRemove,
        IVirtualDesktop_Win11_24H2_Preview* pFallbackDesktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE FindDesktop(
        const GUID* pGuid,
        IVirtualDesktop_Win11_24H2_Preview** ppDesktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDesktopSwitchIncludeExcludeViews(
        IVirtualDesktop_Win11_24H2_Preview* pDesktop,
        IObjectArray** ppViews1,
        IObjectArray** ppViews2) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDesktopName(
        IVirtualDesktop_Win11_24H2_Preview* pDesktop,
        HSTRING name) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDesktopWallpaper(
        IVirtualDesktop_Win11_24H2_Preview* pDesktop,
        HSTRING path) = 0;
    virtual HRESULT STDMETHODCALLTYPE UpdateWallpaperPathForAllDesktops(
        HSTRING path) = 0;
    virtual HRESULT STDMETHODCALLTYPE CopyDesktopState(
        IApplicationView* pView1,
        IApplicationView* pView2) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDesktopIsPerMonitor(
        BOOL* pfPerMonitor) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDesktopIsPerMonitor(
        BOOL fPerMonitor) = 0;
};

// =============================================================================
// IVirtualDesktopNotification - Callback interface for desktop events
// =============================================================================

// Windows 10 - {C179334C-4295-40D3-BEA1-C654D965605A}
MIDL_INTERFACE("C179334C-4295-40D3-BEA1-C654D965605A")
IVirtualDesktopNotification_Win10 : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE VirtualDesktopCreated(
        IVirtualDesktop_Win10* pDesktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE VirtualDesktopDestroyBegin(
        IVirtualDesktop_Win10* pDesktopDestroyed,
        IVirtualDesktop_Win10* pDesktopFallback) = 0;
    virtual HRESULT STDMETHODCALLTYPE VirtualDesktopDestroyFailed(
        IVirtualDesktop_Win10* pDesktopDestroyed,
        IVirtualDesktop_Win10* pDesktopFallback) = 0;
    virtual HRESULT STDMETHODCALLTYPE VirtualDesktopDestroyed(
        IVirtualDesktop_Win10* pDesktopDestroyed,
        IVirtualDesktop_Win10* pDesktopFallback) = 0;
    virtual HRESULT STDMETHODCALLTYPE ViewVirtualDesktopChanged(
        IApplicationView* pView) = 0;
    virtual HRESULT STDMETHODCALLTYPE CurrentVirtualDesktopChanged(
        IVirtualDesktop_Win10* pDesktopOld,
        IVirtualDesktop_Win10* pDesktopNew) = 0;
};

// Windows 11 21H2/22H2 - {cd403e52-deed-4c13-b437-b98380f2b1e8}
MIDL_INTERFACE("cd403e52-deed-4c13-b437-b98380f2b1e8")
IVirtualDesktopNotification_Win11_21H2 : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE VirtualDesktopCreated(
        IObjectArray* pMonitors,
        IVirtualDesktop_Win11_21H2* pDesktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE VirtualDesktopDestroyBegin(
        IObjectArray* pMonitors,
        IVirtualDesktop_Win11_21H2* pDesktopDestroyed,
        IVirtualDesktop_Win11_21H2* pDesktopFallback) = 0;
    virtual HRESULT STDMETHODCALLTYPE VirtualDesktopDestroyFailed(
        IObjectArray* pMonitors,
        IVirtualDesktop_Win11_21H2* pDesktopDestroyed,
        IVirtualDesktop_Win11_21H2* pDesktopFallback) = 0;
    virtual HRESULT STDMETHODCALLTYPE VirtualDesktopDestroyed(
        IObjectArray* pMonitors,
        IVirtualDesktop_Win11_21H2* pDesktopDestroyed,
        IVirtualDesktop_Win11_21H2* pDesktopFallback) = 0;
    virtual HRESULT STDMETHODCALLTYPE VirtualDesktopIsPerMonitorChanged(BOOL bPerMonitor) = 0;
    virtual HRESULT STDMETHODCALLTYPE VirtualDesktopMoved(
        IObjectArray* pMonitors,
        IVirtualDesktop_Win11_21H2* pDesktop,
        int nOldIndex,
        int nNewIndex) = 0;
    virtual HRESULT STDMETHODCALLTYPE VirtualDesktopNameChanged(
        IVirtualDesktop_Win11_21H2* pDesktop,
        HSTRING name) = 0;
    virtual HRESULT STDMETHODCALLTYPE ViewVirtualDesktopChanged(
        IApplicationView* pView) = 0;
    virtual HRESULT STDMETHODCALLTYPE CurrentVirtualDesktopChanged(
        IObjectArray* pMonitors,
        IVirtualDesktop_Win11_21H2* pDesktopOld,
        IVirtualDesktop_Win11_21H2* pDesktopNew) = 0;
    virtual HRESULT STDMETHODCALLTYPE VirtualDesktopWallpaperChanged(
        IVirtualDesktop_Win11_21H2* pDesktop,
        HSTRING path) = 0;
};

// Windows 11 23H2+ - {b9e5e94d-233e-49ab-af5c-2b4541c3aade}
MIDL_INTERFACE("b9e5e94d-233e-49ab-af5c-2b4541c3aade")
IVirtualDesktopNotification_Win11_23H2 : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE VirtualDesktopCreated(
        IObjectArray* pMonitors,
        IVirtualDesktop_Win11_23H2* pDesktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE VirtualDesktopDestroyBegin(
        IObjectArray* pMonitors,
        IVirtualDesktop_Win11_23H2* pDesktopDestroyed,
        IVirtualDesktop_Win11_23H2* pDesktopFallback) = 0;
    virtual HRESULT STDMETHODCALLTYPE VirtualDesktopDestroyFailed(
        IObjectArray* pMonitors,
        IVirtualDesktop_Win11_23H2* pDesktopDestroyed,
        IVirtualDesktop_Win11_23H2* pDesktopFallback) = 0;
    virtual HRESULT STDMETHODCALLTYPE VirtualDesktopDestroyed(
        IObjectArray* pMonitors,
        IVirtualDesktop_Win11_23H2* pDesktopDestroyed,
        IVirtualDesktop_Win11_23H2* pDesktopFallback) = 0;
    virtual HRESULT STDMETHODCALLTYPE VirtualDesktopIsPerMonitorChanged(BOOL bPerMonitor) = 0;
    virtual HRESULT STDMETHODCALLTYPE VirtualDesktopMoved(
        IObjectArray* pMonitors,
        IVirtualDesktop_Win11_23H2* pDesktop,
        int nOldIndex,
        int nNewIndex) = 0;
    virtual HRESULT STDMETHODCALLTYPE VirtualDesktopNameChanged(
        IVirtualDesktop_Win11_23H2* pDesktop,
        HSTRING name) = 0;
    virtual HRESULT STDMETHODCALLTYPE ViewVirtualDesktopChanged(
        IApplicationView* pView) = 0;
    virtual HRESULT STDMETHODCALLTYPE CurrentVirtualDesktopChanged(
        IObjectArray* pMonitors,
        IVirtualDesktop_Win11_23H2* pDesktopOld,
        IVirtualDesktop_Win11_23H2* pDesktopNew) = 0;
    virtual HRESULT STDMETHODCALLTYPE VirtualDesktopWallpaperChanged(
        IVirtualDesktop_Win11_23H2* pDesktop,
        HSTRING path) = 0;
    virtual HRESULT STDMETHODCALLTYPE VirtualDesktopSwitched(
        IVirtualDesktop_Win11_23H2* pDesktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE RemoteVirtualDesktopConnected(
        IVirtualDesktop_Win11_23H2* pDesktop) = 0;
};

// Windows 11 24H2 Preview (Build 26200+) - {0cd45e71-d927-4f15-8b0a-8fef525337bf} (same as service IID)
// Note: Notification interface for 24H2 preview uses same pattern as 23H2
MIDL_INTERFACE("1ba7cf30-3591-43fa-abfa-4aaf7abeedb7")
IVirtualDesktopNotification_Win11_24H2_Preview : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE VirtualDesktopCreated(
        IObjectArray* pMonitors,
        IVirtualDesktop_Win11_24H2_Preview* pDesktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE VirtualDesktopDestroyBegin(
        IObjectArray* pMonitors,
        IVirtualDesktop_Win11_24H2_Preview* pDesktopDestroyed,
        IVirtualDesktop_Win11_24H2_Preview* pDesktopFallback) = 0;
    virtual HRESULT STDMETHODCALLTYPE VirtualDesktopDestroyFailed(
        IObjectArray* pMonitors,
        IVirtualDesktop_Win11_24H2_Preview* pDesktopDestroyed,
        IVirtualDesktop_Win11_24H2_Preview* pDesktopFallback) = 0;
    virtual HRESULT STDMETHODCALLTYPE VirtualDesktopDestroyed(
        IObjectArray* pMonitors,
        IVirtualDesktop_Win11_24H2_Preview* pDesktopDestroyed,
        IVirtualDesktop_Win11_24H2_Preview* pDesktopFallback) = 0;
    virtual HRESULT STDMETHODCALLTYPE VirtualDesktopIsPerMonitorChanged(BOOL bPerMonitor) = 0;
    virtual HRESULT STDMETHODCALLTYPE VirtualDesktopMoved(
        IObjectArray* pMonitors,
        IVirtualDesktop_Win11_24H2_Preview* pDesktop,
        int nOldIndex,
        int nNewIndex) = 0;
    virtual HRESULT STDMETHODCALLTYPE VirtualDesktopNameChanged(
        IVirtualDesktop_Win11_24H2_Preview* pDesktop,
        HSTRING name) = 0;
    virtual HRESULT STDMETHODCALLTYPE ViewVirtualDesktopChanged(
        IApplicationView* pView) = 0;
    virtual HRESULT STDMETHODCALLTYPE CurrentVirtualDesktopChanged(
        IObjectArray* pMonitors,
        IVirtualDesktop_Win11_24H2_Preview* pDesktopOld,
        IVirtualDesktop_Win11_24H2_Preview* pDesktopNew) = 0;
    virtual HRESULT STDMETHODCALLTYPE VirtualDesktopWallpaperChanged(
        IVirtualDesktop_Win11_24H2_Preview* pDesktop,
        HSTRING path) = 0;
    virtual HRESULT STDMETHODCALLTYPE VirtualDesktopSwitched(
        IVirtualDesktop_Win11_24H2_Preview* pDesktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE RemoteVirtualDesktopConnected(
        IVirtualDesktop_Win11_24H2_Preview* pDesktop) = 0;
};

// =============================================================================
// IVirtualDesktopNotificationService - Service to register for notifications
// =============================================================================

// All Windows versions - {0cd45e71-d927-4f15-8b0a-8fef525337bf}
MIDL_INTERFACE("0cd45e71-d927-4f15-8b0a-8fef525337bf")
IVirtualDesktopNotificationService : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE Register(
        IUnknown* pNotification,
        DWORD* pdwCookie) = 0;
    virtual HRESULT STDMETHODCALLTYPE Unregister(DWORD dwCookie) = 0;
};

// =============================================================================
// IServiceProvider - Standard COM service provider (for getting internal interfaces)
// =============================================================================

// {6d5140c1-7436-11ce-8034-00aa006009fa}
MIDL_INTERFACE("6d5140c1-7436-11ce-8034-00aa006009fa")
IServiceProvider : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE QueryService(
        REFGUID guidService,
        REFIID riid,
        void** ppvObject) = 0;
};

// =============================================================================
// GUID Definitions (inline to avoid multiple definition errors)
// =============================================================================

// These need to be defined in exactly one .cpp file that includes this header
#ifdef VIRTUALDESKTOP_INTEROP_DEFINE_GUIDS

// Public VirtualDesktopManager CLSID
// {aa509086-5ca9-4c25-8f95-589d3c07b48a}
const CLSID CLSID_VirtualDesktopManager = 
    { 0xaa509086, 0x5ca9, 0x4c25, { 0x8f, 0x95, 0x58, 0x9d, 0x3c, 0x07, 0xb4, 0x8a } };

// Public IVirtualDesktopManager IID
// {a5cd92ff-29be-454c-8d04-d82879fb3f1b}
const IID IID_IVirtualDesktopManager = 
    { 0xa5cd92ff, 0x29be, 0x454c, { 0x8d, 0x04, 0xd8, 0x28, 0x79, 0xfb, 0x3f, 0x1b } };

// Immersive Shell CLSID (for getting internal interfaces)
// {c2f03a33-21f5-47fa-b4bb-156362a2f239}
const CLSID CLSID_ImmersiveShell = 
    { 0xc2f03a33, 0x21f5, 0x47fa, { 0xb4, 0xbb, 0x15, 0x63, 0x62, 0xa2, 0xf2, 0x39 } };

// VirtualDesktopNotificationService CLSID
// {c5e0cdca-7b6e-41b2-9fc4-d93975cc467b}
const CLSID CLSID_VirtualDesktopNotificationService = 
    { 0xc5e0cdca, 0x7b6e, 0x41b2, { 0x9f, 0xc4, 0xd9, 0x39, 0x75, 0xcc, 0x46, 0x7b } };

#endif // VIRTUALDESKTOP_INTEROP_DEFINE_GUIDS

// =============================================================================
// Helper to get proper GUIDs based on Windows version
// =============================================================================

struct VirtualDesktopGUIDs {
    GUID iidVirtualDesktop;
    GUID iidVirtualDesktopManagerInternal;
    GUID iidVirtualDesktopNotification;
    
    static VirtualDesktopGUIDs ForVersion(WindowsVirtualDesktopVersion version) {
        VirtualDesktopGUIDs guids = {};
        
        switch (version) {
            case WindowsVirtualDesktopVersion::Win10_1803:
            case WindowsVirtualDesktopVersion::Win10_1809:
            case WindowsVirtualDesktopVersion::Win10_1903:
            case WindowsVirtualDesktopVersion::Win10_1909:
            case WindowsVirtualDesktopVersion::Win10_2004:
            case WindowsVirtualDesktopVersion::Win10_20H2:
            case WindowsVirtualDesktopVersion::Win10_21H1:
            case WindowsVirtualDesktopVersion::Win10_21H2:
            case WindowsVirtualDesktopVersion::Win10_22H2:
                // Windows 10 GUIDs
                guids.iidVirtualDesktop = 
                    { 0xff72ffdd, 0xbe7e, 0x43fc, { 0x9c, 0x03, 0xad, 0x81, 0x68, 0x1e, 0x88, 0xe4 } };
                guids.iidVirtualDesktopManagerInternal = 
                    { 0xf31574d6, 0xb682, 0x4cdc, { 0xbd, 0x56, 0x18, 0x27, 0x86, 0x0a, 0xbe, 0xc6 } };
                guids.iidVirtualDesktopNotification = 
                    { 0xc179334c, 0x4295, 0x40d3, { 0xbe, 0xa1, 0xc6, 0x54, 0xd9, 0x65, 0x60, 0x5a } };
                break;
                
            case WindowsVirtualDesktopVersion::Win11_21H2:
            case WindowsVirtualDesktopVersion::Win11_22H2:
                // Windows 11 21H2/22H2 GUIDs
                guids.iidVirtualDesktop = 
                    { 0x536d3495, 0xb208, 0x4cc9, { 0xae, 0x26, 0xde, 0x81, 0x11, 0x27, 0x5b, 0xf8 } };
                guids.iidVirtualDesktopManagerInternal = 
                    { 0xb2f925b9, 0x5a0f, 0x4d2e, { 0x9f, 0x4d, 0x2b, 0x15, 0x07, 0x59, 0x3c, 0x10 } };
                guids.iidVirtualDesktopNotification = 
                    { 0xcd403e52, 0xdeed, 0x4c13, { 0xb4, 0x37, 0xb9, 0x83, 0x80, 0xf2, 0xb1, 0xe8 } };
                break;
                
            case WindowsVirtualDesktopVersion::Win11_23H2:
            case WindowsVirtualDesktopVersion::Win11_24H2:
                // Windows 11 23H2 and stable 24H2 GUIDs
                guids.iidVirtualDesktop = 
                    { 0x3f07f4be, 0xb107, 0x441a, { 0xaf, 0x0f, 0x39, 0xd8, 0x25, 0x29, 0x07, 0x2c } };
                guids.iidVirtualDesktopManagerInternal = 
                    { 0xa3175f2d, 0x239c, 0x4bd2, { 0x8a, 0xa0, 0xee, 0xba, 0x8b, 0x0b, 0x13, 0x8e } };
                guids.iidVirtualDesktopNotification = 
                    { 0xb9e5e94d, 0x233e, 0x49ab, { 0xaf, 0x5c, 0x2b, 0x45, 0x41, 0xc3, 0xaa, 0xde } };
                break;
                
            case WindowsVirtualDesktopVersion::Win11_24H2_Preview:
            default:
                // Windows 11 24H2 Preview/Insider builds (26200+) - use newer GUIDs
                guids.iidVirtualDesktop = 
                    { 0x9f4c7c69, 0x6ed1, 0x408c, { 0xa3, 0xa9, 0x1c, 0x0f, 0x89, 0xe3, 0xb7, 0xb2 } };
                guids.iidVirtualDesktopManagerInternal = 
                    { 0x53f5ca0b, 0x158f, 0x4124, { 0x90, 0x0c, 0x05, 0x71, 0x58, 0x06, 0x0b, 0x27 } };
                guids.iidVirtualDesktopNotification = 
                    { 0x1ba7cf30, 0x3591, 0x43fa, { 0xab, 0xfa, 0x4a, 0xaf, 0x7a, 0xbe, 0xed, 0xb7 } };
                break;
        }
        
        return guids;
    }
};

}  // namespace VirtualOverlay
