<#
.SYNOPSIS
    Builds Virtual Overlay (EXE + optional MSI) and optionally signs the artifacts.
 
.DESCRIPTION
    Auto-discovers cmake and signtool, builds the C++ project and optionally the
    WiX MSI installer, then signs everything with a Certum cloud certificate.
 
.PARAMETER SkipSign
    Skip code signing (useful for local dev builds).
 
.PARAMETER SkipInstaller
    Skip building the MSI installer.
 
.PARAMETER Config
    Build configuration. Defaults to Release.
 
.PARAMETER Generator
    CMake generator. Auto-detected from installed Visual Studio version if omitted.
 
.PARAMETER TimestampUrl
    RFC3161 timestamp server URL for code signing.
 
.EXAMPLE
    .\build.ps1                      # Build + sign everything
    .\build.ps1 -SkipSign            # Build only, no signing
    .\build.ps1 -SkipInstaller       # Build EXE + sign, skip MSI
#>
param(
    [switch]$SkipSign,
    [switch]$SkipInstaller,
    [string]$Config = "Release",
    [string]$Generator,
    [string]$TimestampUrl = "http://timestamp.digicert.com"
)
 
$ErrorActionPreference = "Stop"
$ExePath = "build\$Config\virtual-overlay.exe"
$MsiPath = "installer\VirtualOverlay.msi"
 
# =============================================================================
# Tool discovery
# =============================================================================
 
function Find-VisualStudio {
    # Returns hashtable with cmake path from the newest VS installation.
    # The generator is queried from cmake itself so it's always correct.
    $vsBase = "C:\Program Files\Microsoft Visual Studio"
    if (-not (Test-Path $vsBase)) { return $null }
 
    # Check newest VS folders first. VS 2025+ uses short version numbers (18, 19, ...),
    # older uses year names (2022, 2019, ...). Short numbers = newer, so sort them first.
    $vsDirs = Get-ChildItem $vsBase -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^\d+$' } |
        Sort-Object { if ([int]$_.Name -lt 100) { 10000 + [int]$_.Name } else { [int]$_.Name } } -Descending
 
    foreach ($vsDir in $vsDirs) {
        $cmakeExe = Get-ChildItem "$($vsDir.FullName)\*\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($cmakeExe) {
            # Ask cmake which Visual Studio generator it defaults to
            $genLine = & $cmakeExe.FullName --help 2>&1 |
                Select-String '^\*\s+Visual Studio \d+' |
                Select-Object -First 1
            $generator = if ($genLine) { ($genLine.ToString().Trim('* ')).Split('=')[0].Trim() } else { $null }
            return @{ CMake = $cmakeExe.FullName; Generator = $generator }
        }
    }
    return $null
}
 
function Find-SignTool {
    $existing = Get-Command signtool -ErrorAction SilentlyContinue
    if ($existing) { return $existing.Source }
 
    $sdkRoot = "C:\Program Files (x86)\Windows Kits\10\bin"
    if (Test-Path $sdkRoot) {
        $found = Get-ChildItem "$sdkRoot\*\x64\signtool.exe" -ErrorAction SilentlyContinue |
            Sort-Object { $_.Directory.Parent.Name } -Descending |
            Select-Object -First 1
        if ($found) { return $found.FullName }
    }
 
    return $null
}
 
# --- Locate tools ---
 
$vs = Find-VisualStudio
$cmake = if ($vs) { $vs.CMake } else { (Get-Command cmake -ErrorAction SilentlyContinue).Source }
if (-not $cmake) {
    Write-Error "cmake not found. Install Visual Studio with C++ Desktop Development."
    exit 1
}
if (-not $Generator) {
    $Generator = if ($vs) { $vs.Generator } else { "Visual Studio 17 2022" }
}
Write-Host "cmake:     $cmake" -ForegroundColor DarkGray
Write-Host "generator: $Generator" -ForegroundColor DarkGray
 
if (-not $SkipSign) {
    $signtool = Find-SignTool
    if (-not $signtool) {
        Write-Error "signtool not found. Install the Windows SDK, or pass -SkipSign to build without signing."
        exit 1
    }
    Write-Host "signtool:  $signtool" -ForegroundColor DarkGray
}
 
if (-not $SkipInstaller) {
    $wix = Get-Command wix -ErrorAction SilentlyContinue
    if (-not $wix) {
        Write-Warning "wix CLI not found - MSI installer will be skipped. Install with: dotnet tool install --global wix"
        $SkipInstaller = $true
    } else {
        Write-Host "wix:       $($wix.Source)" -ForegroundColor DarkGray
    }
}
 
Write-Host ""
 
# =============================================================================
# Build
# =============================================================================
 
Write-Host "=== Generating project ===" -ForegroundColor Cyan
 
# If the build dir has a different generator cached, clean it automatically
$cacheFile = "build\CMakeCache.txt"
if (Test-Path $cacheFile) {
    $cachedGen = (Select-String -Path $cacheFile -Pattern '^CMAKE_GENERATOR:INTERNAL=(.+)$' | ForEach-Object { $_.Matches[0].Groups[1].Value })
    if ($cachedGen -and $cachedGen -ne $Generator) {
        Write-Host "Generator changed ($cachedGen -> $Generator), cleaning build dir..." -ForegroundColor Yellow
        Remove-Item "build\CMakeCache.txt", "build\CMakeFiles" -Recurse -Force -ErrorAction SilentlyContinue
    }
}
 
& $cmake -B build -G $Generator -A x64
if ($LASTEXITCODE -ne 0) { Write-Error "cmake generate failed"; exit $LASTEXITCODE }
 
Write-Host "`n=== Building $Config ===" -ForegroundColor Cyan
& $cmake --build build --config $Config
if ($LASTEXITCODE -ne 0) { Write-Error "cmake build failed"; exit $LASTEXITCODE }
 
if (-not (Test-Path $ExePath)) {
    Write-Error "Build succeeded but EXE not found at $ExePath"
    exit 1
}
Write-Host "EXE: $ExePath" -ForegroundColor Green
 
# =============================================================================
# Sign helper
# =============================================================================
 
function Sign-And-Verify {
    param([string]$FilePath)
    if ($SkipSign) { return }
    if (-not (Test-Path $FilePath)) {
        Write-Warning "File not found, skipping sign: $FilePath"
        return
    }
    Write-Host "`n--- Signing $FilePath ---" -ForegroundColor Cyan
    & $signtool sign /v /fd SHA256 /tr $TimestampUrl /td SHA256 /sha1 E8F2F4F7953A1C77B2C2F6DEF98BF6AD5D02A88B $FilePath
    if ($LASTEXITCODE -ne 0) { Write-Error "Signing failed for $FilePath"; exit $LASTEXITCODE }
 
    Write-Host "--- Verifying $FilePath ---" -ForegroundColor Cyan
    & $signtool verify /pa /v $FilePath
    if ($LASTEXITCODE -ne 0) { Write-Error "Verification failed for $FilePath"; exit $LASTEXITCODE }
    Write-Host "OK: $FilePath" -ForegroundColor Green
}
 
# =============================================================================
# Sign EXE (before MSI packages it)
# =============================================================================
 
Sign-And-Verify $ExePath
 
# =============================================================================
# Installer (packages the already-signed EXE)
# =============================================================================
 
if (-not $SkipInstaller) {
    Write-Host "`n=== Building MSI installer ===" -ForegroundColor Cyan
    Push-Location installer
    $wixArgs = @(
        "build"
        "Package.wxs"
        "-ext", "WixToolset.Util.wixext"
        "-o", "VirtualOverlay.msi"
    )
    & wix @wixArgs
    $wixExit = $LASTEXITCODE
    Pop-Location
    if ($wixExit -ne 0) { Write-Error "WiX build failed"; exit $wixExit }
    Write-Host "MSI: $MsiPath" -ForegroundColor Green
 
    # Sign MSI
    Sign-And-Verify $MsiPath
}
 
# =============================================================================
# Summary
# =============================================================================
 
Write-Host "`n=== Done ===" -ForegroundColor Green
Write-Host "  EXE: $ExePath"
if (-not $SkipInstaller -and (Test-Path $MsiPath)) { Write-Host "  MSI: $MsiPath" }
if ($SkipSign) { Write-Host "  (unsigned)" -ForegroundColor Yellow }
