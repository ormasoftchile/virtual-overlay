# Feature Specification: Virtual Overlay — Desktop Display & System Zoom

**Feature Branch**: `001-virtual-overlay`  
**Created**: February 4, 2026  
**Status**: Draft  
**Input**: User description: "Windows utility with virtual desktop overlay and macOS-style zoom"

---

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Zoom Screen Content (Priority: P1)

As a user working on detailed content (code, images, spreadsheets), I want to quickly magnify any area of my screen by holding a modifier key and scrolling, so I can see fine details without changing application zoom settings or moving closer to my monitor.

**Why this priority**: This is the core accessibility/productivity feature. Users need quick, reversible zoom that works across all applications without per-app configuration. This is the primary differentiator (macOS-style zoom not available natively on Windows).

**Independent Test**: Can be fully tested by holding Ctrl and scrolling the mouse wheel anywhere on screen. Delivers immediate value for accessibility and precision work.

**Acceptance Scenarios**:

1. **Given** the application is running and zoom is at 1.0x, **When** I hold Ctrl and scroll up on the mouse wheel, **Then** the screen magnifies centered on my cursor position
2. **Given** the screen is zoomed in (e.g., 2.0x), **When** I move my cursor toward the edge of the screen, **Then** the zoomed view pans smoothly to follow my cursor, revealing previously hidden areas
3. **Given** the screen is zoomed in, **When** I hold Ctrl and scroll down, **Then** the zoom level decreases toward 1.0x
4. **Given** the screen is zoomed in, **When** I double-tap the modifier key, **Then** the zoom instantly resets to 1.0x (normal view)
5. **Given** zoom is enabled, **When** I pinch outward on a touchpad, **Then** the screen zooms in following the same behavior as scroll wheel

---

### User Story 2 - See Current Virtual Desktop (Priority: P2)

As a user with multiple virtual desktops, I want to see the name and/or number of my current desktop when I switch, so I always know which workspace I'm on without having to open Task View.

**Why this priority**: Virtual desktop users frequently switch contexts. An overlay provides instant orientation without interrupting workflow. Depends on base application infrastructure but can be developed independently of zoom.

**Independent Test**: Can be fully tested by switching between virtual desktops using Win+Ctrl+Arrow. The overlay appears showing the desktop name/number, then fades away.

**Acceptance Scenarios**:

1. **Given** the application is running and I have multiple virtual desktops, **When** I switch to a different desktop (via keyboard or Task View), **Then** an overlay appears showing the desktop name and/or number
2. **Given** the overlay is displayed, **When** 2 seconds pass (default), **Then** the overlay fades out smoothly
3. **Given** I have named my desktop "Work Projects", **When** I switch to that desktop, **Then** the overlay shows "2: Work Projects" (number and name)
4. **Given** I switch desktops rapidly, **When** each switch occurs, **Then** the overlay updates immediately without stacking or flickering

---

### User Story 3 - Configure Application Settings (Priority: P3)

As a user, I want to customize how the overlay looks and how zoom behaves through a visual settings window, so I can tailor the application to my preferences without editing configuration files.

**Why this priority**: Customization improves user satisfaction but is not required for core functionality. Users can use defaults initially; settings enhance long-term usability.

**Independent Test**: Can be tested by opening the settings window from the system tray, changing a setting (e.g., overlay position), and verifying the change takes effect.

**Acceptance Scenarios**:

1. **Given** the application is running, **When** I right-click the system tray icon and select "Settings", **Then** a settings window opens with organized configuration options
2. **Given** the settings window is open, **When** I change the overlay position to "bottom-right" and click Apply, **Then** the next desktop switch shows the overlay in the bottom-right corner
3. **Given** the settings window is open, **When** I change the zoom modifier key from Ctrl to Alt, **Then** zoom now responds to Alt+scroll instead of Ctrl+scroll
4. **Given** I have made changes, **When** I click Cancel, **Then** changes are discarded and previous settings remain active

---

### User Story 4 - Application Runs in Background (Priority: P3)

As a user, I want the application to start automatically with Windows and run quietly in the background, so I can benefit from its features without manual launching or visible windows cluttering my desktop.

**Why this priority**: Background operation is expected for system utilities. Essential for "set and forget" user experience.

**Independent Test**: Can be tested by enabling "Start with Windows" in settings, restarting the computer, and verifying the application is running (tray icon visible, features functional).

**Acceptance Scenarios**:

1. **Given** I have installed the application, **When** I enable "Start with Windows" in settings, **Then** the application launches automatically on next login
2. **Given** the application is running, **When** I look at my desktop, **Then** I see only a system tray icon (no visible windows unless triggered)
3. **Given** the application is running, **When** I right-click the tray icon, **Then** I see a menu with options: Settings, About, Exit
4. **Given** I want to close the application, **When** I select "Exit" from the tray menu, **Then** the application closes completely and zoom/overlay features stop working

---

### User Story 5 - Multi-Monitor Support (Priority: P4)

As a user with multiple monitors, I want zoom and overlay to work correctly across all my displays, so I can use these features regardless of which monitor I'm working on.

**Why this priority**: Multi-monitor setups are common among power users who benefit most from these features. Important for full functionality but not blocking for single-monitor users.

**Independent Test**: Can be tested by moving the cursor to different monitors and verifying zoom operates on the active monitor, and overlay appears on the configured monitor.

**Acceptance Scenarios**:

1. **Given** I have two monitors and my cursor is on the secondary monitor, **When** I hold Ctrl and scroll, **Then** the secondary monitor zooms (not the primary)
2. **Given** I have configured overlay to show on "monitor with cursor", **When** I switch desktops while my cursor is on the secondary monitor, **Then** the overlay appears on the secondary monitor
3. **Given** I am zoomed in on one monitor, **When** I move my cursor to another monitor, **Then** zoom resets on the first monitor and I can zoom the new monitor independently

---

### Edge Cases

- What happens when Windows virtual desktop feature is disabled or unavailable? → Application gracefully disables overlay feature and logs the condition
- What happens when another application (e.g., Windows Magnifier) is already controlling zoom? → Application detects conflict, warns user, and disables zoom feature
- What happens when the user is on the Windows lock screen or secure desktop? → Zoom may not function; application remains stable and resumes when returning to normal desktop
- What happens when display settings change (resolution, DPI, monitor added/removed)? → Application detects changes and adjusts overlay position and zoom calculations accordingly
- What happens when the configuration file is corrupted or missing? → Application uses default settings and optionally recreates the configuration file
- What happens at maximum zoom (10x)? → Further scroll-up is ignored; zoom stays at maximum level
- What happens when the user scrolls very fast? → Zoom transitions remain smooth; intermediate levels may be skipped for responsiveness
- What happens when the application crashes or is force-terminated while zoomed in? → Zoom automatically resets to 1.0x when app terminates (normal or crash)

---

## Requirements *(mandatory)*

### Functional Requirements

**Zoom Feature:**

- **FR-001**: System MUST magnify the screen when the user holds the configured modifier key and scrolls the mouse wheel
- **FR-002**: System MUST zoom centered on the current cursor position
- **FR-003**: System MUST pan the zoomed view smoothly as the cursor moves toward screen edges (continuous cursor-following)
- **FR-004**: System MUST support zoom levels from 1.0x (normal) to a configurable maximum (default 10x)
- **FR-005**: System MUST allow configurable zoom step increment per scroll notch (default 0.25x)
- **FR-006**: System MUST provide smooth animated transitions when zooming in/out
- **FR-007**: System MUST reset zoom to 1.0x when the user double-taps the modifier key
- **FR-008**: System MUST support touchpad pinch gestures for zoom in/out
- **FR-009**: System MUST zoom the monitor where the cursor is currently located (multi-monitor support)

**Virtual Desktop Overlay:**

- **FR-010**: System MUST display an overlay when the user switches virtual desktops
- **FR-011**: System MUST show the desktop number and/or name in the overlay (configurable format)
- **FR-012**: System MUST auto-hide the overlay after a configurable timeout (default 2 seconds)
- **FR-013**: System MUST support configurable overlay position (top-left, top-center, top-right, bottom-left, bottom-center, bottom-right, center)
- **FR-014**: System MUST display the overlay with a modern translucent appearance (blur effect matching Windows 11 style)
- **FR-015**: System MUST animate the overlay appearance and disappearance (fade and optional slide)

**Configuration & Settings:**

- **FR-016**: System MUST provide a graphical settings window for configuration
- **FR-017**: System MUST persist user settings between application restarts
- **FR-018**: System MUST allow enabling/disabling zoom and overlay features independently
- **FR-019**: System MUST support configurable modifier key for zoom (Ctrl, Alt, Shift, Win, or combinations)
- **FR-020**: System MUST allow customization of overlay appearance (font, colors, opacity, size)

**System Integration:**

- **FR-021**: System MUST display a system tray icon when running
- **FR-022**: System MUST provide a tray icon context menu with Settings, About, and Exit options
- **FR-023**: System MUST support optional "Start with Windows" functionality
- **FR-024**: System MUST run as a background process with minimal resource usage
- **FR-025**: System MUST log operational events to a file for debugging (app start/stop, errors, zoom levels) without logging sensitive content such as window titles or desktop names

**Compatibility:**

- **FR-026**: System MUST support Windows 10 version 1803 and later
- **FR-027**: System MUST support Windows 11 (all versions)
- **FR-028**: System MUST handle high-DPI displays correctly
- **FR-029**: System MUST respect Windows "reduce motion" accessibility setting for animations

---

### Key Entities

- **Configuration**: User preferences including zoom settings, overlay settings, and general application options. Stored persistently. Editable via GUI or file.

- **Virtual Desktop**: A Windows workspace identified by a number (1-based index) and optional user-defined name. The application monitors for desktop changes.

- **Overlay**: A transient visual element displayed on screen to show desktop information. Has position, appearance, and timing properties.

- **Zoom State**: Current magnification level (1.0x to max) and pan offset. Tied to cursor position and active monitor.

---

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Users can zoom from 1.0x to 2.0x in under 1 second using Ctrl+scroll
- **SC-002**: Zoomed view pans smoothly with cursor movement at 60 FPS (no visible stutter)
- **SC-003**: Desktop switch overlay appears within 100ms of switching desktops
- **SC-004**: Application uses less than 50MB of memory during normal operation
- **SC-005**: Application adds less than 1ms latency to mouse/keyboard input
- **SC-006**: 95% of users can successfully zoom and reset without reading documentation (intuitive)
- **SC-007**: Application starts in under 2 seconds on standard hardware
- **SC-008**: Overlay correctly displays on all Windows 10 (1803+) and Windows 11 versions tested
- **SC-009**: Settings changes take effect immediately without requiring application restart
- **SC-010**: Application handles display configuration changes (resolution, DPI, monitor changes) without crashing

---

## Clarifications

### Session 2026-02-04

- Q: If the application crashes or is force-terminated while zoomed in, what should happen? → A: Zoom automatically resets to 1.0x when app terminates (normal or crash)
- Q: What information should be logged, and how should sensitive data be handled? → A: Operational logging only (app start/stop, errors, zoom levels) — no window/desktop names

---

## Assumptions

- Users have a mouse with scroll wheel or a touchpad with gesture support
- Windows virtual desktops feature is enabled (for overlay functionality)
- No other application is actively controlling system-wide magnification
- User has standard user permissions (admin not required for normal operation)
- Display drivers are functioning correctly for blur/transparency effects

---

## Dependencies

- Windows 10 version 1803+ or Windows 11
- Virtual desktop feature (built into Windows 10/11)
- Standard input devices (keyboard, mouse/touchpad)

---

## Out of Scope (Future Enhancements)

- Hotkeys to rename virtual desktops
- Hotkeys to switch to specific desktop by number (1-9)
- Desktop-specific wallpaper switching
- Window count per desktop in overlay
- Lens mode (magnify only area around cursor)
- Color filters when zoomed (invert, grayscale)
- Per-application profiles
- Localization/internationalization
