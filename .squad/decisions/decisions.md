# Decisions

## 2026-05-02: Remove portable EXE from release workflow

Date: 2026-05-02
Requested by: Cristian Ormazabal

### Decision

Remove all portable EXE handling from `.github/workflows/build.yml` so CI releases only the MSI artifact.

### Why

The portable EXE distribution is no longer a useful release artifact. The MSI is the intended release package, so publishing the EXE adds unnecessary artifact handling and release clutter.

### Scope

- Delete the EXE artifact upload step from the build job.
- Delete the EXE artifact download step from the release job.
- Remove `virtual-overlay.exe` from the GitHub release `files:` list.

### Result

Tagged releases produced by GitHub Actions now publish only `VirtualOverlay.msi`.

---

## 2026-05-02: Installer history findings

Date: 2026-05-02
Requested by: Cristian Ormazabal

### Findings

1. **`build.yml` did not switch from `Product.wxs` to `Package.wxs`.**
   - `git log --follow -p -- .github/workflows/build.yml` shows the workflow was introduced in commit `82e327c08c8a92722012f61f9971918d68efb482` on 2026-02-04.
   - In that initial version, `.github/workflows/build.yml` lines 32-35 already read:
     - line 32: `- name: Build MSI Installer`
     - line 33: `working-directory: installer`
     - line 34: `run: |`
     - line 35: `wix build Package.wxs -o VirtualOverlay.msi`
   - Later commits touching `build.yml` (`8d063a5d640cfd3139012ec8685de0b26439765b`, `6481ecd8e48f182672d8296a193d88dd103a0312`, `1a8c55171c9991fb049f7540312ed728da0c34ac`) changed runner/release packaging only; none changed the WiX build command.

2. **There is no evidence in tracked GitHub Actions history that CI ever built `Product.wxs` or `UI.wxs`.**
   - Searching workflow history with `git log --all -p -G "Product\\.wxs|UI\\.wxs|Package\\.wxs" -- .github/workflows` returns the single workflow-add commit `82e327c08c8a92722012f61f9971918d68efb482`, and that workflow builds `Package.wxs`.
   - No workflow diff in repository history shows `wix build Product.wxs` or `wix build UI.wxs`.

3. **Tagged releases do not predate any CI switch, because no CI switch exists.**
   - `git tag --sort=-creatordate` returns:
     - `Beta 2026-03-11 17:16:04 -0700 1a8c551`
     - `Alpha 2026-02-05 04:34:49 -0800 c42f1a7`
   - The CI workflow was added on 2026-02-04 in `82e327c08c8a92722012f61f9971918d68efb482`, before both tags.
   - Therefore, based on git tags alone, there is no tagged release before a `Product.wxs`→`Package.wxs` workflow change.

4. **`installer/README.md` still documents a manual `Product.wxs` build.**
   - Current `installer/README.md` lines 27-30 say:
     - `wix build installer/Product.wxs installer/UI.wxs \`
     - `    -d BuildDir=build/Release \`
     - `    -ext WixToolset.UI.wixext \`
     - `    -o build/VirtualOverlay-1.0.0.msi`
   - Current `installer/README.md` line 93 also says: `Update Version in Product.wxs`.
   - This documentation came in the initial installer commit `d7326d32a8c93972cc730bd1c6f40a5aa576495e` and still points at a manual `Product.wxs`/`UI.wxs` flow.

5. **The current CI pipeline cannot produce a Program Files install from the checked-in `Package.wxs`.**
   - Current `.github/workflows/build.yml` line 35 builds `Package.wxs`.
   - Current `installer/Package.wxs` line 7 sets `Scope="perUser"`.
   - Current `installer/Package.wxs` lines 16-17 root `INSTALLFOLDER` under `LocalAppDataFolder`, not `ProgramFiles64Folder`.
   - Current `installer/Package.wxs` lines 29-34 write `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\VirtualOverlay`.
   - I found no condition, property override, or alternate directory UI in `Package.wxs` that would redirect this package to Program Files.

### Relevant file evidence

#### HEAD `.github/workflows/build.yml`
- line 35: `wix build Package.wxs -o VirtualOverlay.msi`

#### HEAD `installer/Package.wxs`
- line 7: `Scope="perUser"`
- line 16: `<StandardDirectory Id="LocalAppDataFolder">`
- line 17: `<Directory Id="INSTALLFOLDER" Name="VirtualOverlay">`
- lines 29-34: HKCU Run registration for `VirtualOverlay`

#### HEAD `installer/Product.wxs`
- lines 36-40: `ConfigurableDirectory="INSTALLFOLDER"`
- line 46: `WixUI_InstallDir`
- lines 61-64: `<StandardDirectory Id="ProgramFiles64Folder">`

### Bottom line

Git history does **not** show CI ever switching from `Product.wxs` to `Package.wxs`; CI has built `Package.wxs` from day one of `build.yml`. The repository still documents a separate manual `Product.wxs` + `UI.wxs` build flow, and that is the only tracked path here that matches a Program Files installer.

---

## 2026-05-02: Sal startup challenge

### Finding

The startup reliability analysis needs to treat this as a state-divergence problem, not just a missing-tray problem.

### Evidence

- CI/release builds `installer/Package.wxs`, which always writes `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\VirtualOverlay` to `[INSTALLFOLDER]virtual-overlay.exe` and installs under `%LocalAppData%`.
- `installer/README.md` still documents building `Product.wxs` + `UI.wxs`, which is a different installer family (`UpgradeCode` differs), targets `ProgramFiles64Folder`, and does not create the Run value on install.
- The tray menu rewrites the same HKCU Run value using `GetModuleFileNameW`, so a portable EXE run can repoint startup to an arbitrary path.
- The settings checkbox only saves `general.startWithWindows` into `config.json`; it does not write or repair the Run key.
- Tray icon creation is one-shot with no `TaskbarCreated` recovery.

### Implication

A user can have any of these mismatched states:
1. config says start with Windows, Run key missing;
2. Run key present but points to stale portable path;
3. app started successfully but tray icon never appeared because Explorer/notification area was late;
4. different machines installed different MSI families.

### Recommended verification order

1. Inspect HKCU Run value data, not just presence.
2. Confirm whether the installed binary path matches the Run value target.
3. Check whether `virtual-overlay.exe` is running at logon even when no tray icon appears.
4. Determine which installer family was used on the affected machine.

---

## 2026-05-02: Installer release path and Run key fix

Date: 2026-05-02
Requested by: Cristian Ormazabal
Author: Anne Westphall

### Decision

Make `installer/Package.wxs` the documented release MSI path and quote the HKCU `Run` value target as `&quot;[INSTALLFOLDER]virtual-overlay.exe&quot;`.

### Why

Signed releases installed from the wrong MSI family missed autostart registration entirely, and even the correct per-user installer would fail to launch at logon if `%LocalAppData%` contained spaces in the user profile path.

### Scope

- Update `installer/Package.wxs` so the `Run` key data is quoted for shell parsing.
- Rewrite `installer/README.md` build instructions to require building `Package.wxs` only.
- Preserve existing non-build guidance such as prerequisites, assets, testing notes, and other reference material.

### Result

Release documentation now points to `wix build Package.wxs -o VirtualOverlay.msi`, calls out `%LocalAppData%\VirtualOverlay\` as the install path, states that autostart is created at install time, and warns not to build deprecated `Product.wxs` or `UI.wxs`.

---

## 2026-05-02: Installer Consolidation

Date: 2026-05-02
Requested by: Cristian Ormazabal
Author: Icer Addis

### Context

The shipped signed MSI came from the legacy manual `Product.wxs` path, while CI has long produced the per-user `Package.wxs` MSI. The two installers are different Windows products because they use different `UpgradeCode` values. That split is the source of the release/process confusion and the missing auto-start behavior.

### Decisions

#### 1) `Product.wxs` disposition
**Keep it in-tree, but explicitly deprecate it and remove it from the supported release path.**

Reasoning:
- `Product.wxs` represents a distinct MSI product family because it has a different `UpgradeCode` than `Package.wxs`.
- Deleting it immediately would discard the most concrete reference for the legacy product identity if we later choose to author a cleanup or migration experience.
- It should no longer be treated as a valid build target for releases. Its role is archival/reference-only until the team decides whether to ship an explicit migration/uninstall bridge.

Scope boundary:
- No further feature work should land in `Product.wxs`.
- Mark it as legacy/deprecated in docs/comments.
- Supported releases should standardize on `Package.wxs` only.

#### 2) `UI.wxs` disposition
**Deprecate it alongside `Product.wxs` and treat it as legacy-only.**

Reasoning:
- `UI.wxs` exists only to extend the legacy `Product.wxs` flow.
- Its launch-on-exit and cleanup behavior are not part of the supported installer architecture once `Package.wxs` becomes the only release definition.
- Keeping it as an archival companion to `Product.wxs` preserves historical context without implying that it remains part of the shipping path.

#### 3) `Package.wxs` Run key quoting
**Yes, quote the executable path in the Run value.**

Required form:
- `Value="&quot;[INSTALLFOLDER]virtual-overlay.exe&quot;"`

Reasoning:
- `[INSTALLFOLDER]` resolves under `%LOCALAPPDATA%`, which lives under the user profile path.
- Windows profile directory names can contain spaces, so `LocalAppData`-based paths are not guaranteed to be space-free.
- Unquoted Run entries are parsed by the shell/process creation rules as command lines, not as raw paths, so a space-bearing profile path can break startup.

Architecture note:
- Quoting the executable path is the correct default even when the current machine's profile path has no spaces.

#### 4) `installer/README.md` update scope
**Rewrite the README so it documents only the supported installer flow.**

It should say:
- Build **`installer/Package.wxs`** for release candidates and signed releases.
- Use the WiX CLI command that matches the current repo flow, e.g. from `installer/`:
  - `wix build Package.wxs -o VirtualOverlay.msi`
- Expected install location:
  - `%LOCALAPPDATA%\VirtualOverlay`
- Installer behavior note:
  - The installer is **per-user** and writes `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\VirtualOverlay` so the app starts at logon.
- Signing note:
  - CI can build the unsigned MSI artifact, but the release MSI is built and code-signed locally before manual upload/publication.
- Legacy note:
  - `Product.wxs` and `UI.wxs` are deprecated legacy assets and are not the supported release path.

#### 5) `.github/workflows/build.yml` release trigger
**Leave the tag trigger alone for this consolidation; do not widen it right now.**

Reasoning:
- Releases are effectively manual because signing happens locally, not in CI.
- Broadening the trigger to `refs/tags/*` would increase the chance of CI creating unsigned/tag-driven releases that do not match the real shipping process.
- The real issue here is installer-source selection, not GitHub tag automation.

Follow-up recommendation:
- Revisit tag semantics only when the release process itself is redesigned (for example, if signed artifacts or a deliberate draft-release workflow become CI-driven).

### Outcome
The supported installer architecture should be a single per-user MSI path built from `Package.wxs`; the legacy `Product.wxs`/`UI.wxs` pair remains only as deprecated reference material until the team decides whether to author an explicit migration story for past installs.
