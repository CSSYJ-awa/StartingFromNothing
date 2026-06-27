# ============================================================
#   set_env.ps1 -- One-click environment setup for VulkanApp
#   Windows 10/11
#   Requires admin rights (for setting user-level env vars)
# ============================================================

#Requires -RunAsAdministrator

# ============================================================
# Helper functions
# ============================================================
function Write-Color {
    param([string]$Message, [ConsoleColor]$Color = "White")
    $original = $host.UI.RawUI.ForegroundColor
    $host.UI.RawUI.ForegroundColor = $Color
    Write-Host $Message
    $host.UI.RawUI.ForegroundColor = $original
}

function Write-Info  { param([string]$Msg)  Write-Color "  [INFO] $Msg" -Color Blue   }
function Write-Ok   { param([string]$Msg)  Write-Color "  [OK]   $Msg" -Color Green  }
function Write-Warn { param([string]$Msg)  Write-Color "  [WARN] $Msg" -Color Yellow }
function Write-Fail { param([string]$Msg)  Write-Color "  [FAIL] $Msg" -Color Red    }

function Test-EnvVar($Name) {
    $val = [Environment]::GetEnvironmentVariable($Name, "User")
    if (-not $val) { $val = [Environment]::GetEnvironmentVariable($Name, "Machine") }
    if (-not $val) { $val = [Environment]::GetEnvironmentVariable($Name, "Process") }
    return $val
}

# ============================================================
# Auto-detect MSYS2 installation path
# ============================================================
function Find-Msys2Root {
    $candidates = @(
        $env:MSYS2_ROOT
        "C:\msys64"
        "C:\msys2"
        "$env:ProgramFiles\msys64"
        "C:\Program Files (x86)\msys64"
        "D:\msys64"
        "D:\msys2"
        "D:\Program Files\msys64"
        "E:\msys64"
        "E:\msys2"
    )

    # Try to infer from g++ in PATH
    $gpp = Get-Command "g++.exe" -ErrorAction SilentlyContinue
    if ($gpp) {
        $dir = Split-Path (Split-Path $gpp.Source) -Parent
        if (Test-Path "$dir\etc\pacman.conf") {
            Write-Info "Found MSYS2 root from g++ path: $dir"
            return $dir
        }
    }

    foreach ($path in $candidates) {
        if ($path -and (Test-Path "$path\etc\pacman.conf")) {
            Write-Info "Found MSYS2 at $path"
            return $path
        }
    }

    return $null
}

# ============================================================
# Auto-detect MSYS2 subsystem type
# ============================================================
function Find-Msys2Subsystem($msys2Root) {
    $subsystems = @("ucrt64", "mingw64", "clang64", "mingw32")
    foreach ($sub in $subsystems) {
        if (Test-Path "$msys2Root\$sub\bin\g++.exe") {
            Write-Info "Detected active subsystem: $sub"
            return $sub
        }
    }
    return "ucrt64"
}

# ============================================================
# Main
# ============================================================
Clear-Host
Write-Color "============================================" -Color Cyan
Write-Color "   VulkanApp - Environment Setup            " -Color Cyan
Write-Color "============================================" -Color Cyan
Write-Host ""

# ----------------------------------------------------------
# 1. Detect MSYS2 and set MSYS2_ROOT
# ----------------------------------------------------------
Write-Color "--- [1/3] MSYS2 ---" -Color Yellow

$msys2Root = Find-Msys2Root
if (-not $msys2Root) {
    Write-Fail "MSYS2 not found!"
    Write-Host "  Please install MSYS2: https://www.msys2.org/"
    Write-Host "  Then re-run this script."
    exit 1
}

$subsystem = Find-Msys2Subsystem $msys2Root
Write-Ok "MSYS2 root: $msys2Root"
Write-Ok "Subsystem: $subsystem"

$currentMsys2 = Test-EnvVar "MSYS2_ROOT"
if ($currentMsys2 -and $currentMsys2 -eq $msys2Root) {
    Write-Ok "MSYS2_ROOT already set correctly: $msys2Root"
} else {
    [Environment]::SetEnvironmentVariable("MSYS2_ROOT", $msys2Root, "User")
    $env:MSYS2_ROOT = $msys2Root
    Write-Ok "MSYS2_ROOT set to: $msys2Root"
}

Write-Host "  -> Compiler: $msys2Root\$subsystem\bin\g++.exe"
if (Test-Path "$msys2Root\$subsystem\bin\g++.exe") {
    Write-Ok "Compiler available"
} else {
    Write-Warn "Compiler not found, check MSYS2 installation"
}

# ----------------------------------------------------------
# 2. Check Vulkan SDK
# ----------------------------------------------------------
Write-Color "--- [2/3] Vulkan SDK ---" -Color Yellow

$vulkanSdk = Test-EnvVar "VULKAN_SDK"
if (-not $vulkanSdk) {
    $vulkanCandidates = @(
        "$env:ProgramFiles\VulkanSDK\*",
        "D:\Program Files\VulkanSDK\*"
    )
    $found = $false
    foreach ($pattern in $vulkanCandidates) {
        $dirs = Get-ChildItem $pattern -Directory -ErrorAction SilentlyContinue
        if ($dirs) {
            $latest = $dirs | Sort-Object Name -Descending | Select-Object -First 1
            $vulkanSdk = $latest.FullName
            $found = $true
            break
        }
    }
    if ($found) {
        [Environment]::SetEnvironmentVariable("VULKAN_SDK", $vulkanSdk, "User")
        $env:VULKAN_SDK = $vulkanSdk
        Write-Ok "VULKAN_SDK set to: $vulkanSdk"
    } else {
        Write-Warn "Vulkan SDK not found!"
        Write-Host "  Download from: https://vulkan.lunarg.com/"
    }
} else {
    Write-Ok "VULKAN_SDK already set: $vulkanSdk"
}

$vulkanHeader = "$vulkanSdk\Include\vulkan\vulkan.h"
if (Test-Path $vulkanHeader) {
    Write-Ok "vulkan/vulkan.h found"
} else {
    Write-Warn "vulkan/vulkan.h not found, check Vulkan SDK installation"
}

# ----------------------------------------------------------
# 3. Check dependencies
# ----------------------------------------------------------
Write-Color "--- [3/3] Dependencies ---" -Color Yellow

$glfwHeader = "$msys2Root\mingw64\include\GLFW\glfw3.h"
if (Test-Path $glfwHeader) {
    Write-Ok "GLFW found (mingw64)"
} else {
    Write-Warn "GLFW not found. Run: pacman -S mingw-w64-x86_64-glfw"
}

$glmHeader = "$msys2Root\mingw64\include\glm\glm.hpp"
if (Test-Path $glmHeader) {
    Write-Ok "GLM found (mingw64)"
} else {
    Write-Warn "GLM not found. Run: pacman -S mingw-w64-x86_64-glm"
}

# ----------------------------------------------------------
# Done
# ----------------------------------------------------------
Write-Host ""
Write-Color "============================================" -Color Cyan
Write-Color "   Setup Complete!                          " -Color Cyan
Write-Color "============================================" -Color Cyan
Write-Host ""
Write-Host "  Environment variables set:"
if (Test-EnvVar "MSYS2_ROOT") {
    Write-Host "    MSYS2_ROOT = $msys2Root"
}
if (Test-EnvVar "VULKAN_SDK") {
    Write-Host "    VULKAN_SDK = $vulkanSdk"
}
Write-Host ""
Write-Host "  Reload VS Code window to apply changes."
Write-Host "  (Command: Developer: Reload Window)"
Write-Host ""