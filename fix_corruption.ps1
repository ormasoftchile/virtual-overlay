$file = "c:\One\OpenSource\virtual-overlay\src\desktop\VirtualDesktop.cpp"
$content = Get-Content $file -Raw

# Find the end marker of HStringToWString (after its closing brace)
$hsEnd = $content.IndexOf("return L`"`";`r`n}")
if ($hsEnd -lt 0) {
    $hsEnd = $content.IndexOf("return L`"`";`n}")
}
$hsEnd = $content.IndexOf("}", $hsEnd) + 1
Write-Host "HStringToWString ends around position: $hsEnd"

# Find where good notification handler code starts
$win11Pos = $content.IndexOf("VirtualDesktopNotification_Win11_21H2::QueryInterface")
if ($win11Pos -gt 0) {
    # Back up to find the comment line
    $lineStart = $content.LastIndexOf("`n", $win11Pos)
    if ($lineStart -gt 0) {
        $prevLine = $content.LastIndexOf("`n", $lineStart - 1)
        if ($prevLine -gt 0 -and $content.Substring($prevLine, $lineStart - $prevLine).Contains("Windows 11")) {
            $win11Pos = $prevLine + 1
        } else {
            $win11Pos = $lineStart + 1
        }
    }
}
Write-Host "Win11 notification handler starts at: $win11Pos"

if ($hsEnd -gt 0 -and $win11Pos -gt $hsEnd) {
    # The good code to insert
    $replacement = @'

void VirtualDesktop::SetDesktopSwitchCallback(DesktopSwitchCallback callback) {
    m_switchCallback = std::move(callback);
    
    if (m_usingPolling && m_switchCallback) {
        m_lastKnownDesktopIndex = 1;
        
        m_desktopSwitchHook = SetWinEventHook(
            0x0020, 0x0020, nullptr, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT
        );
        
        if (m_desktopSwitchHook) {
            LOG_INFO("Installed WinEvent hook for desktop switch detection");
        } else {
            m_pollingTimer = SetTimer(nullptr, 0, 200, PollingTimerProc);
            LOG_DEBUG("Started desktop polling timer");
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
    
    HWND hForeground = GetForegroundWindow();
    if (!hForeground) {
        return;
    }
    
    GUID currentDesktopId = {};
    HRESULT hr = m_pPublicVirtualDesktopManager->GetWindowDesktopId(hForeground, &currentDesktopId);
    if (FAILED(hr) || IsEqualGUID(currentDesktopId, GUID{})) {
        return;
    }
    
    if (!IsEqualGUID(currentDesktopId, m_lastKnownDesktopId)) {
        m_lastKnownDesktopId = currentDesktopId;
        m_lastKnownDesktopIndex = (m_lastKnownDesktopIndex % 10) + 1;
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

'@

    # Build new content: beginning + replacement + rest
    $beforeCorruption = $content.Substring(0, $hsEnd)
    $afterCorruption = $content.Substring($win11Pos)
    $newContent = $beforeCorruption + $replacement + $afterCorruption
    
    # Write the fixed file
    Set-Content -Path $file -Value $newContent -NoNewline -Encoding UTF8
    Write-Host "File fixed successfully! New length: $($newContent.Length)"
} else {
    Write-Host "Could not find markers for replacement. hsEnd=$hsEnd, win11Pos=$win11Pos"
}
