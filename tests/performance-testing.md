# Performance and Memory Testing Guide

This document describes procedures for verifying Virtual Overlay meets performance requirements.

## Requirements Summary

| Metric | Requirement | Reference |
|--------|-------------|-----------|
| Peak Memory | < 50 MB | SC-004 |
| Input Latency | < 1 ms | SC-005 |
| Handle Leaks | None | Best practice |
| Memory Leaks | None | Best practice |

---

## 1. AppVerifier Testing

### Installation

1. Download [Application Verifier](https://docs.microsoft.com/en-us/windows-hardware/drivers/devtest/application-verifier) from Windows SDK
2. Install via Windows SDK installer or standalone download

### Configuration

```powershell
# Add application to AppVerifier (run as Administrator)
appverif.exe /verify virtual-overlay.exe

# Or use GUI:
# 1. Run appverif.exe
# 2. File → Add Application → Select virtual-overlay.exe
# 3. Enable these tests:
#    - Basics: Handles, Heaps, Locks, Memory
#    - Miscellaneous: DangerousAPIs, DirtyStacks
```

### Testing Procedure

1. Start Virtual Overlay with AppVerifier attached
2. Perform comprehensive usage:
   - Zoom in/out multiple times
   - Switch virtual desktops 10+ times
   - Open/close settings window
   - Change settings and apply
   - Toggle auto-start on/off
3. Exit application via tray menu
4. Check AppVerifier logs for violations

### Expected Results

- **Handles**: No orphaned handles (GDI, User, Kernel)
- **Heaps**: No heap corruption or leaks
- **Memory**: No buffer overruns
- **Locks**: No deadlocks or lock ordering violations

---

## 2. Memory Usage Testing (SC-004)

### Peak Memory < 50 MB

#### Method 1: Task Manager

1. Start Virtual Overlay
2. Open Task Manager (Ctrl+Shift+Esc)
3. Find "virtual-overlay.exe" in process list
4. Right-click → Go to details
5. Add column: "Peak working set (memory)"
6. Perform intensive operations:
   - Zoom to max level
   - Pan around screen rapidly
   - Switch desktops repeatedly
7. Record peak memory value

#### Method 2: Performance Monitor

```powershell
# Create data collector set
logman create counter VirtualOverlayMemory -cf counters.txt -si 1 -f csv -o memory_log

# counters.txt content:
# \Process(virtual-overlay)\Working Set Peak
# \Process(virtual-overlay)\Private Bytes
# \Process(virtual-overlay)\Virtual Bytes
```

#### Method 3: PowerShell Script

```powershell
# Monitor memory every second for 5 minutes
$process = Get-Process virtual-overlay
$samples = @()
for ($i = 0; $i -lt 300; $i++) {
    $samples += $process.PeakWorkingSet64 / 1MB
    Start-Sleep 1
    $process = Get-Process virtual-overlay
}
Write-Host "Peak Memory: $([math]::Max($samples)) MB"
```

### Pass Criteria

| Check | Target | Actual |
|-------|--------|--------|
| Peak working set | < 50 MB | ____ MB |
| Private bytes (1hr idle) | < 30 MB | ____ MB |
| No continuous growth | Stable | [ ] Yes [ ] No |

---

## 3. Input Latency Testing (SC-005)

### Latency < 1 ms

Input latency is measured as time from WM_MOUSEWHEEL receipt to MagSetFullscreenMagnification call.

#### Method 1: Code Instrumentation

Add timing code to InputHook.cpp:

```cpp
// In HandleLowLevelInput or mouse hook
auto start = std::chrono::high_resolution_clock::now();
// ... zoom operation ...
auto end = std::chrono::high_resolution_clock::now();
auto latency = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
OutputDebugString(std::format(L"Input latency: {} µs\n", latency.count()).c_str());
```

#### Method 2: ETW Tracing

1. Use Windows Performance Recorder (WPR)
2. Capture CPU + Input trace
3. Analyze with Windows Performance Analyzer (WPA)
4. Measure time from input event to magnification API call

#### Method 3: Perceived Latency

Visual test (subjective):

1. Zoom in to 2x
2. Move cursor rapidly
3. Observe magnified view tracking
4. Should feel immediate with no perceptible lag

### Pass Criteria

| Check | Target | Actual |
|-------|--------|--------|
| Input processing time | < 1 ms (1000 µs) | ____ µs |
| Perceived lag | None | [ ] Pass [ ] Fail |

---

## 4. Handle Leak Testing

### Using Process Explorer

1. Download [Process Explorer](https://docs.microsoft.com/en-us/sysinternals/downloads/process-explorer)
2. Find virtual-overlay.exe
3. View → Select Columns → Process Performance → Handles
4. Monitor handle count over time
5. Perform stress test operations

### Using Handle.exe

```powershell
# Snapshot handles
handle.exe -p virtual-overlay > handles_before.txt

# Perform operations...

handle.exe -p virtual-overlay > handles_after.txt

# Compare
Compare-Object (Get-Content handles_before.txt) (Get-Content handles_after.txt)
```

### Pass Criteria

| Check | Target | Actual |
|-------|--------|--------|
| GDI handles stable | No growth | [ ] Yes [ ] No |
| User handles stable | No growth | [ ] Yes [ ] No |
| Kernel handles stable | No growth | [ ] Yes [ ] No |

---

## 5. Long-Running Stability Test

### 8-Hour Soak Test

1. Start Virtual Overlay
2. Record initial memory/handles
3. Every hour:
   - Zoom in/out 5 times
   - Switch desktops 5 times
   - Record memory/handles
4. After 8 hours, compare metrics

### Automated Soak Script

```powershell
$log = "soak_test_log.csv"
"Time,WorkingSet,PeakWorkingSet,Handles" | Out-File $log

$endTime = (Get-Date).AddHours(8)
while ((Get-Date) -lt $endTime) {
    $proc = Get-Process virtual-overlay -ErrorAction SilentlyContinue
    if ($proc) {
        "$((Get-Date).ToString('HH:mm:ss')),$($proc.WorkingSet64/1MB),$($proc.PeakWorkingSet64/1MB),$($proc.HandleCount)" | 
            Add-Content $log
    }
    Start-Sleep 60
}
```

### Pass Criteria

| Check | Target | Actual |
|-------|--------|--------|
| Memory stable over 8hr | ±10% | [ ] Yes [ ] No |
| Handles stable | ±5 | [ ] Yes [ ] No |
| No crashes | 0 | [ ] Yes [ ] No |

---

## Test Results Summary

| Test | Date | Tester | Result |
|------|------|--------|--------|
| AppVerifier | | | |
| Peak Memory | | | |
| Input Latency | | | |
| Handle Leaks | | | |
| Soak Test | | | |

**Build Version**: ____________________  
**Test Environment**: Windows __ (build ____)  
**Overall Result**: [ ] PASS [ ] FAIL
