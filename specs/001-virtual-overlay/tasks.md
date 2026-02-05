# Tasks: Virtual Overlay

**Input**: Design documents from `/specs/001-virtual-overlay/`  
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, quickstart.md

## Format: `[ID] [P?] [Story?] Description`

- **[P]**: Can run in parallel (different files, no dependencies on incomplete tasks)
- **[Story]**: Which user story (US1-US5) — only in user story phases
- Include exact file paths in descriptions

## Path Conventions

Single native Win32 application:
- **Source**: `src/` at repository root
- **Resources**: `res/`
- **Tests**: `tests/` (manual test plan only)
- **Installer**: `installer/`
- **Dependencies**: `lib/`

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project initialization, build system, and directory structure

- [X] T001 Create project directory structure per plan.md layout
- [X] T002 Create CMakeLists.txt with C++17, static CRT, Windows SDK dependencies
- [X] T003 [P] Download nlohmann/json header to lib/json.hpp
- [X] T004 [P] Create res/app.manifest with DPI awareness and Windows 10/11 compatibility
- [X] T005 [P] Create res/app.rc with version info and icon resource
- [X] T006 [P] Create placeholder res/icon.ico (32x32, 48x48, 256x256)
- [X] T007 Verify build compiles empty Win32 app (cmake configure + build)

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

- [X] T008 Create src/utils/Logger.h/.cpp with file logging, daily rotation, level filtering
- [X] T009 [P] Create src/config/Defaults.h with all default configuration values from data-model.md
- [X] T010 Create src/config/Config.h/.cpp with JSON load/save, validation, merge with defaults
- [X] T011 Create src/main.cpp with WinMain, single-instance check, message loop
- [X] T012 Create src/App.h/.cpp application controller with Init/Run/Shutdown lifecycle
- [X] T013 [P] Create src/utils/Monitor.h/.cpp with multi-monitor enumeration utilities
- [X] T014 [P] Create src/utils/Animation.h/.cpp with easing functions (linear, ease-out, ease-in)
- [X] T015 Integrate Config and Logger into App initialization
- [X] T016 Verify app starts, loads config (or creates default), and logs startup

**Checkpoint**: Foundation ready — user story implementation can now begin

---

## Phase 3: User Story 1 — Zoom Screen Content (Priority: P1) 🎯 MVP

**Goal**: macOS-style Ctrl+scroll zoom with cursor-following pan

**Independent Test**: Hold Ctrl and scroll mouse wheel anywhere on screen. Screen magnifies centered on cursor. Move cursor to edge — view pans. Double-tap Ctrl — zoom resets.

### Implementation for User Story 1

- [X] T017 Create src/zoom/ZoomConfig.h struct with zoom settings (modifierKey, zoomStep, maxZoom, smoothing, etc.)
- [X] T018 Create src/zoom/Magnifier.h/.cpp wrapping MagInitialize, MagSetFullscreenTransform, MagUninitialize
- [X] T019 Create src/zoom/ZoomController.h/.cpp state machine (NORMAL, ZOOMING) with level/offset management
- [X] T020 Create src/input/GlobalHooks.h/.cpp with SetWindowsHookEx for WH_KEYBOARD_LL and WH_MOUSE_LL
- [X] T021 Implement modifier key detection (Ctrl held state tracking) in GlobalHooks
- [X] T022 Implement mouse wheel capture when modifier held, PostMessage to main window in GlobalHooks
- [X] T023 Implement cursor-following pan algorithm in ZoomController (normX/normY → offset calculation)
- [X] T024 Implement smooth zoom animation with target/current interpolation in ZoomController; log zoom level changes to debug log (FR-025)
- [X] T025 Implement double-tap modifier detection for zoom reset in GlobalHooks
- [X] T026 Create src/input/GestureHandler.h/.cpp with WM_GESTURE/GID_ZOOM touchpad pinch support
- [X] T027 Wire ZoomController and Magnifier into App, handle WM_USER_ZOOM messages
- [X] T028 Add zoom reset on application exit (MagUninitialize ensures cleanup)
- [X] T029 Test: Verify Ctrl+scroll zooms, pan works, double-tap resets, pinch works

**Checkpoint**: User Story 1 complete — zoom feature is fully functional and independently testable

---

## Phase 4: User Story 2 — See Current Virtual Desktop (Priority: P2)

**Goal**: Show desktop name/number overlay when switching virtual desktops

**Independent Test**: Press Win+Ctrl+Right to switch desktops. Overlay appears showing "2: Desktop Name", fades out after 2 seconds.

### Implementation for User Story 2

- [X] T030 Create src/overlay/OverlayConfig.h struct with overlay settings (position, format, style, animation)
- [X] T031 Create src/utils/D2DRenderer.h/.cpp with Direct2D factory, render target, DirectWrite text layout
- [X] T032 Create src/overlay/AcrylicHelper.h/.cpp with DwmSetWindowAttribute (Win11) and SetWindowCompositionAttribute (Win10 fallback)
- [X] T033 Create src/overlay/OverlayWindow.h/.cpp with layered window creation, WS_EX_TOPMOST, WS_EX_NOACTIVATE
- [X] T034 Implement Direct2D rendering of rounded rect background and text in OverlayWindow
- [X] T035 Implement acrylic blur effect application in OverlayWindow using AcrylicHelper
- [X] T036 Create src/desktop/VirtualDesktopInterop.h with version-specific COM GUIDs for Windows 10/11 builds
- [X] T037 Create src/desktop/VirtualDesktop.h/.cpp with COM initialization, IVirtualDesktopManagerInternal access
- [X] T038 Implement GetCurrentDesktop() returning index and name in VirtualDesktop
- [X] T039 Implement desktop change notification registration (IVirtualDesktopNotification) in VirtualDesktop
- [X] T040 Implement overlay state machine (HIDDEN → FADE_IN → VISIBLE → FADE_OUT → HIDDEN) in OverlayWindow
- [X] T041 Implement fade-in animation (opacity 0→1 over 150ms with optional slide) in OverlayWindow
- [X] T042 Implement fade-out animation (opacity 1→0 over 200ms) with auto-hide timer in OverlayWindow
- [X] T043 Implement format string substitution ({number}, {name}) for overlay text
- [X] T044 Implement position calculation for all 7 positions (top-left, top-center, etc.) in OverlayWindow
- [X] T045 Wire VirtualDesktop notifications to OverlayWindow.Show() in App
- [X] T046 Add fallback: If COM fails, enumerate desktops by index only (no names) with warning log
- [X] T047 Test: Verify Win+Ctrl+Arrow shows overlay, correct name, fades out, rapid switching works

**Checkpoint**: User Story 2 complete — overlay feature is fully functional and independently testable

---

## Phase 5: User Story 3 — Configure Application Settings (Priority: P3)

**Goal**: GUI settings window accessible from system tray

**Independent Test**: Right-click tray icon → Settings. Change overlay position, click Apply. Switch desktops — overlay appears in new position.

### Implementation for User Story 3

- [X] T048 Create src/settings/SettingsWindow.h/.cpp with modeless dialog window, tab control
- [X] T049 [P] Create src/settings/SettingsPages.h/.cpp with page definitions (General, Overlay, Zoom, About)
- [X] T050 Implement General tab: Start with Windows checkbox, Show tray icon checkbox, Settings hotkey display
- [X] T051 Implement Overlay tab: Enable checkbox, position dropdown, format text, font/color pickers, opacity slider, auto-hide duration
- [X] T052 Implement Zoom tab: Enable checkbox, modifier key dropdown, zoom step slider, max zoom slider, smoothing checkbox, touchpad pinch checkbox
- [X] T053 Implement About tab: Version info, GitHub link
- [X] T054 Implement Apply button: Persist config, notify App to reload settings
- [X] T055 Implement Cancel button: Discard changes, close window
- [X] T056 Implement Preview button (Overlay tab): Temporarily show overlay with current settings
- [X] T057 Implement live settings update: Settings changes apply immediately to running features
- [X] T058 Wire SettingsWindow.Open() to tray menu and global hotkey (Ctrl+Shift+O default)
- [X] T059 Test: Verify all settings persist, changes take effect, Cancel discards changes

**Checkpoint**: User Story 3 complete — settings window is fully functional

---

## Phase 6: User Story 4 — Application Runs in Background (Priority: P3)

**Goal**: Tray icon with context menu, auto-start with Windows

**Independent Test**: Run application — only tray icon visible. Right-click shows menu. Enable "Start with Windows", restart — app launches automatically.

### Implementation for User Story 4

- [X] T060 Create src/tray/TrayIcon.h/.cpp with Shell_NotifyIcon, custom icon, tooltip
- [X] T061 Implement tray icon context menu: Settings, About, Exit
- [X] T062 Implement Settings menu item: Open SettingsWindow
- [X] T063 Implement About menu item: Show About dialog (version, links)
- [X] T064 Implement Exit menu item: Clean shutdown, MagUninitialize, remove tray icon
- [X] T065 Implement "Start with Windows" functionality via Registry Run key (HKCU\...\Run)
- [X] T066 Handle WM_DISPLAYCHANGE for monitor configuration changes (reinitialize monitors) — Note: Enhances US1/US2 retroactively; zoom and overlay re-adapt to new monitor config
- [X] T067 Handle WM_DPICHANGED for DPI-awareness (recalculate overlay position/size) — Note: Enhances US1/US2 retroactively
- [X] T068 Ensure single instance: Mutex check in main.cpp, activate existing window if already running
- [X] T069 Wire tray icon into App lifecycle (create on Init, remove on Shutdown)
- [X] T070 Test: Verify tray icon visible, menu works, Exit cleans up, auto-start works

**Checkpoint**: User Story 4 complete — background operation is fully functional

---

## Phase 7: User Story 5 — Multi-Monitor Support (Priority: P4)

**Goal**: Zoom and overlay work correctly on multi-monitor setups

**Independent Test**: With 2 monitors, cursor on secondary, Ctrl+scroll — secondary monitor zooms. Switch desktop with cursor on secondary — overlay appears on secondary.

### Implementation for User Story 5

- [X] T071 Enhance Monitor.cpp: Get monitor from cursor position (MonitorFromPoint)
- [X] T072 Enhance ZoomController: Track active monitor, reset zoom when cursor moves to different monitor
- [X] T073 Enhance Magnifier: Apply zoom transform to correct monitor bounds
- [X] T074 Enhance OverlayWindow: Calculate position based on configured monitor (cursor/primary/all)
- [X] T075 Implement "all monitors" overlay mode: Create overlay window per monitor, show on all
- [X] T076 Handle monitor hotplug: Re-enumerate monitors on WM_DISPLAYCHANGE, adjust active state
- [X] T077 Test: Verify zoom on correct monitor, overlay on correct monitor, monitor changes handled

**Checkpoint**: User Story 5 complete — multi-monitor support is fully functional

---

## Phase 8: Polish & Cross-Cutting Concerns

**Purpose**: Final improvements, documentation, installer

- [X] T078 [P] Implement Windows "reduce motion" accessibility check, disable animations if enabled
- [X] T079 [P] Add conflict detection: Check if Windows Magnifier is active, warn user and disable zoom
- [X] T080 Implement graceful handling of virtual desktop feature unavailable (log, disable overlay)
- [X] T081 Add AppVerifier testing for memory/handle leaks; verify peak memory <50MB using Task Manager/perfmon (SC-004); measure input latency <1ms (SC-005)
- [X] T082 [P] Create installer/Product.wxs WiX MSI definition
- [X] T083 [P] Create installer/UI.wxs WiX UI customization
- [X] T084 Build and test MSI installer (install, run, uninstall)
- [X] T085 [P] Create tests/test-plan.md with manual test procedures for all features
- [X] T086 Final code cleanup: Remove debug code, verify all TODOs resolved
- [X] T087 Run quickstart.md validation: Clone fresh, follow steps, verify all features work

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies — can start immediately
- **Phase 2 (Foundational)**: Depends on Phase 1 — BLOCKS all user stories
- **Phase 3-7 (User Stories)**: All depend on Phase 2 completion
  - Stories can proceed in priority order (P1 → P2 → P3 → P4)
  - Or in parallel if team capacity allows
- **Phase 8 (Polish)**: Depends on desired user stories being complete

### User Story Dependencies

| Story | Depends On | Can Start After |
|-------|------------|-----------------|
| US1 (Zoom) | Phase 2 only | T016 complete |
| US2 (Overlay) | Phase 2 only | T016 complete |
| US3 (Settings) | US1, US2 (they must exist to configure) | T047 complete |
| US4 (Background) | Phase 2, uses Settings from US3 | T016 complete (tray), T059 complete (menu→settings) |
| US5 (Multi-Monitor) | US1, US2 (enhances them) | T029, T047 complete |

**Note**: US1 and US2 are independent and can be developed in parallel after Foundational phase.

### Within Each Phase

1. Non-[P] tasks: Execute in order (dependencies exist)
2. [P] tasks: Can run in parallel with other [P] tasks at same level
3. Complete all tasks in phase before marking checkpoint complete

---

## Parallel Opportunities

### Phase 1 Setup (T003-T006 parallel)

```
T001 → T002 → parallel(T003, T004, T005, T006) → T007
```

### Phase 2 Foundational (T009, T013, T014 parallel)

```
T008 → parallel(T009, T013, T014) → T010 → T011 → T012 → T015 → T016
```

### User Stories in Parallel

After Phase 2, with multiple developers:
- Developer A: US1 (Zoom) — T017-T029
- Developer B: US2 (Overlay) — T030-T047
- Then: US3 (Settings), US4 (Background), US5 (Multi-Monitor)

---

## Implementation Strategy

### MVP Delivery (User Story 1 Only)

1. Complete Phase 1: Setup (T001-T007)
2. Complete Phase 2: Foundational (T008-T016)
3. Complete Phase 3: User Story 1 - Zoom (T017-T029)
4. **STOP AND VALIDATE**: Test Ctrl+scroll zoom works
5. Deliverable: Minimal zoom utility

### Incremental Delivery

1. MVP: Setup + Foundational + US1 (Zoom) → Zoom works
2. Increment 1: Add US2 (Overlay) → Desktop switching shows overlay
3. Increment 2: Add US4 (Background) → Tray icon, clean exit
4. Increment 3: Add US3 (Settings) → GUI configuration
5. Increment 4: Add US5 (Multi-Monitor) → Full multi-monitor support
6. Final: Polish phase → Installer, accessibility, conflict detection

---

## Notes

- No automated tests — manual testing per quickstart.md
- [P] tasks can run concurrently (different files)
- Verify each checkpoint before proceeding
- COM GUIDs in T036 must match target Windows version
- T046 fallback critical for robustness across Windows versions
