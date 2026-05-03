# Performance and Memory Testing Guide

This document describes procedures for verifying Virtual Overlay meets performance requirements.

## Requirements Summary

| Metric | Requirement | Reference |
|--------|-------------|-----------|
| Peak Memory | < 50 MB | SC-004 |
| Desktop Switch Latency | < 100 ms | SC-005 |
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
```

### Testing Procedure

1. Start Virtual Overlay with AppVerifier attached
2. Perform comprehensive usage:
   - Switch virtual desktops 10+ times
   - Open/close settings window
   - Change overlay settings and apply
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
4. Right-click  Go to details
5. Add column: "Peak working set (memory)"
6. Perform intensive operations:
   - Switch desktops repeatedly
   - Open Settings and Preview several times
7. Record peak memory value

#### Method 2: PowerShell Script

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

## 3. Desktop Switch Latency Testing (SC-005)

### Latency < 100 ms

#### Method 1: Visual Timing

1. Start Virtual Overlay
2. Switch desktops with `Win+Ctrl+Left/Right`
3. Verify the overlay appears nearly immediately after the switch
4. Repeat at least 20 times

#### Method 2: ETW Tracing

1. Use Windows Performance Recorder (WPR)
2. Capture CPU + UI trace
3. Analyze with Windows Performance Analyzer (WPA)
4. Measure time from desktop switch to overlay render

### Pass Criteria

| Check | Target | Actual |
|-------|--------|--------|
| Desktop switch to overlay render | < 100 ms | ____ ms |
| Perceived lag | Minimal | [ ] Pass [ ] Fail |

---

## 4. Handle Leak Testing

### Using Process Explorer

1. Download [Process Explorer](https://docs.microsoft.com/en-us/sysinternals/downloads/process-explorer)
2. Find virtual-overlay.exe
3. View  Select Columns  Process Performance  Handles
4. Monitor handle count over time
5. Perform stress test operations

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
   - Switch desktops 5 times
   - Open and close Settings
   - Record memory/handles
4. After 8 hours, compare metrics

### Pass Criteria

| Check | Target | Actual |
|-------|--------|--------|
| Memory stable over 8hr | 10% | [ ] Yes [ ] No |
| Handles stable | 5 | [ ] Yes [ ] No |
| No crashes | 0 | [ ] Yes [ ] No |

---

## Test Results Summary

| Test | Date | Tester | Result |
|------|------|--------|--------|
| AppVerifier | | | |
| Peak Memory | | | |
| Desktop Switch Latency | | | |
| Handle Leaks | | | |
| Soak Test | | | |

**Build Version**: ____________________  
**Test Environment**: Windows __ (build ____)  
**Overall Result**: [ ] PASS [ ] FAIL
