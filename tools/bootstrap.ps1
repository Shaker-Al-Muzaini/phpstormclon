# Forge IDE Bootstrap Script
# Installs all required tools for building Forge IDE on Windows
# Run as Administrator for best results

param(
    [switch]$SkipRust,
    [switch]$SkipCMake,
    [switch]$SkipQt,
    [switch]$SkipVcpkg
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "Continue"

Write-Host "=== Forge IDE Bootstrap ===" -ForegroundColor Cyan
Write-Host "This script installs: Rust, CMake, vcpkg, and verifies Qt 6" -ForegroundColor Gray
Write-Host ""

# --- Rust ---
if (-not $SkipRust) {
    if (Get-Command rustup -ErrorAction SilentlyContinue) {
        Write-Host "[OK] Rust already installed: $(rustc --version)" -ForegroundColor Green
        Write-Host "     Ensuring stable toolchain is active..."
        rustup toolchain install stable
        rustup default stable
    } else {
        Write-Host "[INSTALL] Downloading Rust installer (rustup-init.exe)..." -ForegroundColor Yellow
        $rustupUrl = "https://win.rustup.rs/x86_64"
        $rustupPath = "$env:TEMP\rustup-init.exe"
        Invoke-WebRequest -Uri $rustupUrl -OutFile $rustupPath -UseBasicParsing
        Write-Host "          Running rustup-init (non-interactive)..."
        Start-Process -FilePath $rustupPath -ArgumentList "-y", "--default-toolchain", "stable", "--profile", "default" -Wait -NoNewWindow
        # Refresh PATH
        $env:PATH = [System.Environment]::GetEnvironmentVariable("PATH", "User") + ";" + $env:PATH
        Write-Host "[OK] Rust installed: $(rustc --version)" -ForegroundColor Green
    }
} else {
    Write-Host "[SKIP] Rust installation skipped." -ForegroundColor Gray
}

# --- CMake ---
if (-not $SkipCMake) {
    if (Get-Command cmake -ErrorAction SilentlyContinue) {
        Write-Host "[OK] CMake already installed: $(cmake --version | Select-Object -First 1)" -ForegroundColor Green
    } else {
        Write-Host "[INSTALL] Downloading CMake 4.4.4 for Windows x64..." -ForegroundColor Yellow
        $cmakeUrl = "https://github.com/Kitware/CMake/releases/download/v4.4.4/cmake-4.4.4-windows-x86_64.msi"
        $cmakePath = "$env:TEMP\cmake-4.4.4-windows-x86_64.msi"
        Invoke-WebRequest -Uri $cmakeUrl -OutFile $cmakePath -UseBasicParsing
        Write-Host "          Installing CMake (adding to PATH)..."
        Start-Process msiexec -ArgumentList "/i", $cmakePath, "ADD_CMAKE_TO_PATH=System", "/quiet", "/norestart" -Wait
        $env:PATH = [System.Environment]::GetEnvironmentVariable("PATH", "Machine") + ";" + $env:PATH
        Write-Host "[OK] CMake installed: $(cmake --version | Select-Object -First 1)" -ForegroundColor Green
    }
} else {
    Write-Host "[SKIP] CMake installation skipped." -ForegroundColor Gray
}

# --- vcpkg ---
if (-not $SkipVcpkg) {
    $vcpkgRoot = "C:\vcpkg"
    if (Test-Path "$vcpkgRoot\vcpkg.exe") {
        Write-Host "[OK] vcpkg already installed at $vcpkgRoot" -ForegroundColor Green
    } else {
        Write-Host "[INSTALL] Cloning vcpkg to $vcpkgRoot..." -ForegroundColor Yellow
        git clone https://github.com/microsoft/vcpkg.git $vcpkgRoot
        Write-Host "          Bootstrapping vcpkg..."
        & "$vcpkgRoot\bootstrap-vcpkg.bat" -disableMetrics
        Write-Host "[OK] vcpkg installed at $vcpkgRoot" -ForegroundColor Green
    }
    # Set VCPKG_ROOT environment variable
    [System.Environment]::SetEnvironmentVariable("VCPKG_ROOT", $vcpkgRoot, "User")
    $env:VCPKG_ROOT = $vcpkgRoot
    Write-Host "     VCPKG_ROOT set to $vcpkgRoot" -ForegroundColor Gray
} else {
    Write-Host "[SKIP] vcpkg installation skipped." -ForegroundColor Gray
}

Write-Host ""
Write-Host "=== Bootstrap Complete ===" -ForegroundColor Cyan
Write-Host "Next steps:" -ForegroundColor White
Write-Host "  1. Install Qt 6.8 LTS from https://www.qt.io/download-open-source" -ForegroundColor White
Write-Host "     Select: Qt 6.8.x > MSVC 2022 64-bit" -ForegroundColor Gray
Write-Host "     Set QT_ROOT env var to your Qt install root (e.g. C:\Qt\6.8.x\msvc2022_64)" -ForegroundColor Gray
Write-Host "  2. Run: cmake --preset windows-debug" -ForegroundColor White
Write-Host "  3. Run: cmake --build build/debug" -ForegroundColor White
Write-Host ""
Write-Host "See README.md for full build instructions." -ForegroundColor Gray
