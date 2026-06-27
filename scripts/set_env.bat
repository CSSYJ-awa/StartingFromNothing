@echo off
chcp 65001 >nul 2>&1
title VulkanApp - Setup Environment Variables

:: ============================================================================
:: set_env.bat -- One-click environment variable setup for VulkanApp
:: Usage: Right-click -> "Run as administrator"
:: ============================================================================

setlocal enabledelayedexpansion

:: ---- ANSI colors ----
for /f %%e in ('echo prompt $E ^| cmd') do set "ESC=%%e"
set "GREEN=%ESC%[92m"
set "RED=%ESC%[91m"
set "YELLOW=%ESC%[93m"
set "CYAN=%ESC%[96m"
set "RESET=%ESC%[0m"

echo.
echo %CYAN%============================================%RESET%
echo %CYAN%   VulkanApp - Setup Environment Variables  %RESET%
echo %CYAN%============================================%RESET%
echo.

:: Check administrator privileges
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo %RED%  [FAIL] Please run this script as Administrator!%RESET%
    echo         Right-click set_env.bat ^> "Run as administrator"
    pause
    exit /b 1
)

:: ============================================================================
:: 1. Detect MSYS2
:: ============================================================================
echo %YELLOW%--- [1/3] MSYS2 ---%RESET%

set "MSYS2_ROOT="

:: Try to detect from g++ in PATH first
for /f "delims=" %%i in ('where g++.exe 2^>nul') do (
    set "GPP_PATH=%%i"
    call :resolve_from_gpp
    if defined MSYS2_ROOT goto :found_msys2
)

:: Check candidate paths
call :check_candidate "C:\msys64"
if defined MSYS2_ROOT goto :found_msys2
call :check_candidate "C:\msys2"
if defined MSYS2_ROOT goto :found_msys2
if defined ProgramFiles call :check_candidate "%ProgramFiles%\msys64"
if defined MSYS2_ROOT goto :found_msys2
call :check_candidate "D:\msys64"
if defined MSYS2_ROOT goto :found_msys2
call :check_candidate "D:\msys2"
if defined MSYS2_ROOT goto :found_msys2
call :check_candidate "D:\Program Files\msys64"
if defined MSYS2_ROOT goto :found_msys2
call :check_candidate "E:\msys64"
if defined MSYS2_ROOT goto :found_msys2
call :check_candidate "E:\msys2"

:found_msys2
if not defined MSYS2_ROOT (
    echo %RED%  [FAIL] MSYS2 not found!%RESET%
    echo         Please install MSYS2 from: https://www.msys2.org/
    pause
    exit /b 1
)
echo %GREEN%  [OK]    MSYS2 root: %MSYS2_ROOT%%RESET%

:: Detect subsystem (ucrt64/mingw64/clang64)
set "SUBSYSTEM=ucrt64"
if exist "%MSYS2_ROOT%\ucrt64\bin\g++.exe" set "SUBSYSTEM=ucrt64"
if exist "%MSYS2_ROOT%\mingw64\bin\g++.exe" set "SUBSYSTEM=mingw64"
if exist "%MSYS2_ROOT%\clang64\bin\g++.exe" set "SUBSYSTEM=clang64"
echo %GREEN%  [OK]    Subsystem: %SUBSYSTEM%%RESET%

:: Set MSYS2_ROOT as user environment variable
setx MSYS2_ROOT "%MSYS2_ROOT%" >nul 2>&1
if %errorlevel% equ 0 (
    echo %GREEN%  [OK]    MSYS2_ROOT = %MSYS2_ROOT%%RESET%
) else (
    echo %RED%  [FAIL] Failed to set MSYS2_ROOT%RESET%
)

:: Verify compiler
if exist "%MSYS2_ROOT%\%SUBSYSTEM%\bin\g++.exe" (
    echo %GREEN%  [OK]    Compiler: %MSYS2_ROOT%\%SUBSYSTEM%\bin\g++.exe%RESET%
) else (
    echo %YELLOW%  [WARN]  Compiler not found, check MSYS2 installation%RESET%
)

:: ============================================================================
:: 2. Detect Vulkan SDK
:: ============================================================================
echo.
echo %YELLOW%--- [2/3] Vulkan SDK ---%RESET%

:: Check existing VULKAN_SDK
if defined VULKAN_SDK (
    echo %GREEN%  [OK]    VULKAN_SDK already set: %VULKAN_SDK%%RESET%
    goto :check_vulkan_header
)

:: Auto-detect
set "VULKAN_SDK="
for /f "delims=" %%v in ('dir /b /ad "%ProgramFiles%\VulkanSDK" 2^>nul') do set "VULKAN_SDK=%ProgramFiles%\VulkanSDK\%%v"
if not defined VULKAN_SDK for /f "delims=" %%v in ('dir /b /ad "D:\Program Files\VulkanSDK" 2^>nul') do set "VULKAN_SDK=D:\Program Files\VulkanSDK\%%v"

if defined VULKAN_SDK (
    setx VULKAN_SDK "%VULKAN_SDK%" >nul 2>&1
    echo %GREEN%  [OK]    VULKAN_SDK = %VULKAN_SDK%%RESET%
) else (
    echo %YELLOW%  [WARN]  Vulkan SDK not found%RESET%
    echo         Download from: https://vulkan.lunarg.com/
)

:check_vulkan_header
if defined VULKAN_SDK (
    if exist "%VULKAN_SDK%\Include\vulkan\vulkan.h" (
        echo %GREEN%  [OK]    vulkan/vulkan.h found%RESET%
    ) else (
        echo %YELLOW%  [WARN]  vulkan/vulkan.h not found%RESET%
    )
)

:: ============================================================================
:: 3. Check dependencies
:: ============================================================================
echo.
echo %YELLOW%--- [3/3] Dependencies ---%RESET%

if exist "%MSYS2_ROOT%\mingw64\include\GLFW\glfw3.h" (
    echo %GREEN%  [OK]    GLFW found (mingw64)%RESET%
) else (
    echo %YELLOW%  [WARN]  GLFW not found%RESET%
    echo         Run: pacman -S mingw-w64-x86_64-glfw
)

if exist "%MSYS2_ROOT%\mingw64\include\glm\glm.hpp" (
    echo %GREEN%  [OK]    GLM found (mingw64)%RESET%
) else (
    echo %YELLOW%  [WARN]  GLM not found%RESET%
    echo         Run: pacman -S mingw-w64-x86_64-glm
)

:: ============================================================================
:: Done
:: ============================================================================
echo.
echo %CYAN%============================================%RESET%
echo %CYAN%   Setup Complete!                          %RESET%
echo %CYAN%============================================%RESET%
echo.
echo  Environment variables set:
echo    MSYS2_ROOT = %MSYS2_ROOT%
if defined VULKAN_SDK echo    VULKAN_SDK = %VULKAN_SDK%
echo.
echo  IMPORTANT: Reload VS Code window to apply changes.
echo  (Command: Developer: Reload Window)
echo.
pause
exit /b 0

:resolve_from_gpp
for %%a in ("%GPP_PATH%") do set "GPP_DIR=%%~dpa"
for %%b in ("%GPP_DIR%..\..") do set "MSYS2_ROOT=%%~fb"
if not exist "%MSYS2_ROOT%\etc\pacman.conf" set "MSYS2_ROOT="
goto :eof

:check_candidate
set "CHECK_PATH=%~1"
if exist "%CHECK_PATH%\etc\pacman.conf" set "MSYS2_ROOT=%CHECK_PATH%"
goto :eof
