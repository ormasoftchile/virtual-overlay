# Session: Remove portable EXE from release pipeline

**When:** 2026-05-02 10:11:23 UTC  
**What:** Removed portable EXE artifact handling from build.yml  
**Why:** MSI-only release model; EXE no longer needed  
**Files changed:** `.github/workflows/build.yml`

## Changes summary

- Removed EXE artifact upload from build job
- Removed EXE artifact download from release job
- Removed `virtual-overlay.exe` from release files list

**Result:** CI releases only VirtualOverlay.msi
