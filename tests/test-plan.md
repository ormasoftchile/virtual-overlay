# Virtual Overlay Test Plan

**Version**: 1.3.0  
**Last Updated**: February 4, 2026

## Overview

This document describes manual test procedures for verifying Virtual Overlay overlay, watermark, tray, and virtual desktop features prior to release.

## Prerequisites

- Windows 10 version 1803 or later, or Windows 11
- At least one monitor (multi-monitor recommended for full testing)
- Multiple virtual desktops created (Win+Tab  New Desktop)

## Test Categories

1. [Overlay Feature Tests](#1-overlay-feature-tests)
2. [Settings Window Tests](#2-settings-window-tests)
3. [Tray Icon Tests](#3-tray-icon-tests)
4. [Multi-Monitor Tests](#4-multi-monitor-tests)
5. [Edge Cases and Error Handling](#5-edge-cases-and-error-handling)

---

## 1. Overlay Feature Tests

### 1.1 Basic Overlay Display

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Create at least 2 virtual desktops (Win+Tab) | Desktops created |
| 2 | Press Win+Ctrl+Right Arrow | Overlay appears showing "2: Desktop Name" |
| 3 | Wait at least 3 seconds | Watermark remains visible |
| 4 | Press Win+Ctrl+Left Arrow | Overlay shows "1: Desktop 1" |

**Pass**: [ ] **Fail**: [ ]

### 1.2 Watermark Settings

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Open Settings > Overlay tab | Overlay settings visible |
| 2 | Enable the watermark and click Apply | Setting saved; no mode selector or auto-hide controls |
| 3 | Observe desktop | Watermark remains visible |
| 4 | Switch virtual desktops | Watermark text updates to the current desktop |

**Pass**: [ ] **Fail**: [ ]

### 1.3 Overlay Hotkey

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Ensure overlay is enabled | Overlay visible or available |
| 2 | Press Ctrl+Shift+D | Overlay toggles visibility |
| 3 | Press Ctrl+Shift+D again | Overlay toggles back |

**Pass**: [ ] **Fail**: [ ]

### 1.4 Per-Monitor Watermark Opt-Out

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Connect 2 monitors and open Settings > Overlay tab | "Show watermark on these screens" lists both monitors |
| 2 | Uncheck the second monitor and click Apply | Setting saved |
| 3 | Move the cursor to the second monitor (Monitor = Cursor) | Watermark hides |
| 4 | Move the cursor back to an allowed monitor | Watermark reappears |
| 5 | Set Monitor to Primary, opt out the primary, click Apply | Watermark stays hidden |
| 6 | Re-check all monitors and click Apply | Watermark follows the cursor again |
| 7 | Enable flip on the second monitor (Flip Screen tab) and move the cursor there | No watermark on the flipped screen (it would be mirrored) |

**Pass**: [ ] **Fail**: [ ]

### 1.5 Legacy Notification Settings

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Load an older configuration with overlay mode `notification` and auto-hide enabled | Configuration loads without losing watermark appearance or monitor preferences |
| 2 | Start Virtual Overlay on an allowed monitor and wait beyond the former auto-hide delay | Persistent watermark is visible, without popup animation or background |
| 3 | Save settings | JSON no longer contains overlay `mode`, `autoHide`, `autoHideDelayMs`, or `animation` |
| 4 | Disable the watermark, save, and restart | Watermark remains disabled |

Automated regression: `ctest --test-dir build -C Release -R "^(watermark|settings)$" --output-on-failure`.

**Pass**: [ ] **Fail**: [ ]

---

## 2. Settings Window Tests

### 2.1 Open Settings

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Right-click tray icon | Context menu appears |
| 2 | Click "Settings..." | Settings window opens |
| 3 | Verify 5 tabs exist | General, Overlay, Flip Screen, Presenter, About |

**Pass**: [ ] **Fail**: [ ]

### 2.2 Change Overlay Position

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Open Settings > Overlay tab | Overlay settings visible |
| 2 | Change Position dropdown to "Bottom Right" | Selection changes |
| 3 | Click "Preview" button | Overlay appears in bottom-right |
| 4 | Click "Apply" | Settings saved |
| 5 | Switch virtual desktops | Overlay now appears in bottom-right |

**Pass**: [ ] **Fail**: [ ]

### 2.3 Cancel Discards Changes

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Open Settings | Settings window opens |
| 2 | Change several settings | Settings modified in UI |
| 3 | Click "Cancel" | Window closes |
| 4 | Reopen Settings | Previous (saved) values shown |

**Pass**: [ ] **Fail**: [ ]

### 2.4 Typography and Display Scaling

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Open each settings tab at 100%, 125%, 150%, and 200% Windows display scaling | Native UI font, spacing, controls, and footer buttons scale together |
| 2 | Open Presenter and inspect channel/visible-line dropdowns and model download buttons | Captions are readable without clipping or overlap |
| 3 | Change an overlay format, slider, and dropdown without clicking Apply | Values remain pending |
| 4 | Move Settings between monitors with different display scales several times | Layout stays aligned without size drift; pending values and the selected tab remain unchanged |
| 5 | Close with Cancel and reopen | No pending changes were saved; typography remains consistent |

Automated regression: `ctest --test-dir build -C Release -R "^settings$" --output-on-failure`.

**Pass**: [ ] **Fail**: [ ]

---

## 3. Tray Icon Tests

### 3.1 Tray Icon Visibility

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Start Virtual Overlay | Tray icon appears in system tray |
| 2 | Hover over icon | Tooltip shows "Virtual Overlay" |

**Pass**: [ ] **Fail**: [ ]

### 3.2 Context Menu

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Right-click tray icon | Menu appears |
| 2 | Verify menu items | Settings, Start with Windows, About, Exit |
| 3 | Click "About" | About dialog appears |
| 4 | Close About dialog | Dialog closes |

**Pass**: [ ] **Fail**: [ ]

### 3.3 Exit Cleanup

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Ensure overlay is visible | Overlay shown |
| 2 | Click "Exit" from tray menu | Application closes |
| 3 | Verify tray icon removed | Icon no longer in tray |

**Pass**: [ ] **Fail**: [ ]

---

## 4. Multi-Monitor Tests

### 4.1 Overlay on Cursor Monitor

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Ensure overlay set to "Cursor Monitor" | Setting verified |
| 2 | Move cursor to secondary monitor | Cursor on secondary |
| 3 | Switch virtual desktops | Overlay appears on secondary monitor |
| 4 | Move cursor to primary, switch desktops | Overlay appears on primary |

**Pass**: [ ] **Fail**: [ ] **N/A**: [ ]

### 4.2 Monitor Hotplug

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Disconnect a monitor | Monitor removed |
| 2 | Overlay still works | No crash, feature works |
| 3 | Reconnect monitor | Monitor restored |
| 4 | Switch desktops again | Overlay appears correctly |

**Pass**: [ ] **Fail**: [ ] **N/A**: [ ]

---

## 5. Edge Cases and Error Handling

### 5.1 Single Instance

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Start Virtual Overlay | Application running |
| 2 | Try to start second instance | Settings window opens instead |
| 3 | Verify only one tray icon | Single icon present |

**Pass**: [ ] **Fail**: [ ]

### 5.2 Configuration File

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Delete config.json | File deleted |
| 2 | Start application | Default config created |
| 3 | Verify default settings | All defaults applied |

**Pass**: [ ] **Fail**: [ ]

### 5.3 DPI Changes

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Change display scale (Settings > Display) | DPI changes |
| 2 | Overlay appears in correct position | Scaled correctly |
| 3 | No visual glitches | Clean rendering |

**Pass**: [ ] **Fail**: [ ]

---

## Performance Verification

| Check | Target | Result |
|-------|--------|--------|
| Peak memory usage | < 50 MB | [ ] MB |
| Desktop switch overlay | < 100ms | [ ] ms |

---

## Test Results Summary

| Category | Pass | Fail | N/A |
|----------|------|------|-----|
| Overlay Feature | | | |
| Settings Window | | | |
| Tray Icon | | | |
| Multi-Monitor | | | |
| Edge Cases | | | |

**Tester**: ____________________  
**Date**: ____________________  
**Build**: ____________________  
**Overall Result**: [ ] PASS [ ] FAIL
