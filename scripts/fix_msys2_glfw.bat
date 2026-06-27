@echo off
:: Switch console to UTF-8 (65001) for correct Chinese display
chcp 65001 >nul 2>&1

setlocal enabledelayedexpansion
title MSYS2 GLFW / GLM Diagnostic Tool

:: ============================================================================
:: fix_msys2_glfw.bat -- MSYS2 GLFW/GLM diagnostic and repair wizard
::
:: MSYS2 installs libraries under /mingw64/ (/ucrt64/) but CMake launched
:: from Windows CMD does not search these Unix-style paths by default.
:: This script auto-detects the MSYS2 root and locates GLFW/GLM files.
::
:: Usage: scripts\fix_msys2_glfw.bat
:: ============================================================================

:: ---- 获取 ESC 字符用于 ANSI 彩色输出 ----
for /f %%e in ('echo prompt $E ^| cmd') do set "ESC=%%e"
set "GREEN=%ESC%[92m"
set "RED=%ESC%[91m"
set "YELLOW=%ESC%[93m"
set "CYAN=%ESC%[96m"
set "RESET=%ESC%[0m"

:: 检测 ANSI 支持
for /f "tokens=4-5 delims=. " %%i in ('ver') do set WIN_VER=%%i.%%j
set ANSI_OK=0
if %WIN_VER% GEQ 10.0 set ANSI_OK=1

:: ---- 辅助子程序 ----
goto :skip_macros

:print_ok
set "_MSG=%~1"
if %ANSI_OK%==1 (echo     %GREEN%[OK]%RESET%    !_MSG!) else (echo     [OK]    !_MSG!)
goto :eof

:print_fail
set "_MSG=%~1"
if %ANSI_OK%==1 (echo     %RED%[FAIL]%RESET%  !_MSG!) else (echo     [FAIL]  !_MSG!)
goto :eof

:print_info
set "_MSG=%~1"
if %ANSI_OK%==1 (echo             %CYAN%!_MSG!%RESET%) else (echo             !_MSG!)
goto :eof

:print_warn
set "_MSG=%~1"
if %ANSI_OK%==1 (echo             %YELLOW%!_MSG!%RESET%) else (echo             !_MSG!)
goto :eof

:separator
echo     ------------------------------------------------
goto :eof
:skip_macros

:: ============================================================================
:: 标题
:: ============================================================================
echo.
echo ================================================================
echo   MSYS2 GLFW / GLM 诊断与修复向导
echo ================================================================
echo.

:: ============================================================================
:: 阶段 1：定位 MSYS2 安装
:: ============================================================================
call :separator
echo   [1] 定位 MSYS2 安装目录
call :separator

set MSYS2_ROOT=

:: ---- 1.1 从环境变量 MSYS2_ROOT 读取 ----
if not "%MSYS2_ROOT%"=="" (
    call :print_ok "从 MSYS2_ROOT 环境变量获取: %MSYS2_ROOT%"
    goto :msys2_found
)

:: ---- 1.2 从编译器路径反推 ----
::     原理: g++ 位于 {MSYS2_ROOT}/{subsys}/bin/g++.exe
::     例如 D:\Program Files\msys64\ucrt64\bin\g++.exe → root = D:\Program Files\msys64
::     使用 %%~dpD 逐级向上获取父目录, 而非拼接 ".." (路径含空格时不可靠)
where g++ >nul 2>&1
if !ERRORLEVEL! EQU 0 (
    for /f "delims=" %%p in ('where g++ 2^>^&1') do set "GXX_PATH=%%p" & goto :got_gxx
)
goto :no_gxx

:got_gxx
call :print_info "g++ 路径: !GXX_PATH!"

:: 第 1 级: 获取 g++.exe 所在目录 (即 bin/)
for %%D in ("!GXX_PATH!") do set "BIN_DIR=%%~dpD"
if "!BIN_DIR:~-1!"=="\" set "BIN_DIR=!BIN_DIR:~0,-1!"

:: 第 2 级: 获取 bin 的父目录 (即子系统目录, 如 ucrt64)
for %%D in ("!BIN_DIR!") do set "SUBSYS_DIR=%%~dpD"
if "!SUBSYS_DIR:~-1!"=="\" set "SUBSYS_DIR=!SUBSYS_DIR:~0,-1!"

:: 第 3 级: 获取子系统的父目录 (即 MSYS2 根目录)
for %%D in ("!SUBSYS_DIR!") do set "MSYS2_ROOT_CANDIDATE=%%~dpD"
if "!MSYS2_ROOT_CANDIDATE:~-1!"=="\" set "MSYS2_ROOT_CANDIDATE=!MSYS2_ROOT_CANDIDATE:~0,-1!"

call :print_info "候选 MSYS2 根目录: !MSYS2_ROOT_CANDIDATE!"

:: 验证是否为 MSYS2 目录 (多项特征交叉验证, 避免误判)
set MSYS2_SIGNATURE=0
:: 特征1: etc/pacman.conf 必定存在于任何 MSYS2 安装
if exist "!MSYS2_ROOT_CANDIDATE!\etc\pacman.conf" set MSYS2_SIGNATURE=1
:: 特征2: 任意 MINGW 子系统的 g++.exe
for %%S in (ucrt64 mingw64 clang64 mingw32 clang32) do (
    if exist "!MSYS2_ROOT_CANDIDATE!\%%S\bin\g++.exe" set MSYS2_SIGNATURE=1
)
:: 特征3: 任意 MINGW 子系统的 include 目录 (即使 GLFW/GLM 未安装, include 目录也存在)
for %%S in (ucrt64 mingw64 clang64) do (
    if exist "!MSYS2_ROOT_CANDIDATE!\%%S\include\GLFW"  set MSYS2_SIGNATURE=1
    if exist "!MSYS2_ROOT_CANDIDATE!\%%S\include\glm"   set MSYS2_SIGNATURE=1
)

if !MSYS2_SIGNATURE!==1 (
    set "MSYS2_ROOT=!MSYS2_ROOT_CANDIDATE!"
    call :print_ok "从编译器路径反推: !MSYS2_ROOT!"
    goto :msys2_found
)

:no_gxx
:: ---- 1.3 尝试常见路径 (含 Program Files 下的安装) ----
call :print_info "从编译器路径反推失败，尝试常见路径..."
for %%P in (
    "C:\msys64"
    "C:\msys2"
    "D:\msys64"
    "D:\msys2"
    "E:\msys64"
    "E:\msys2"
    "C:\Program Files\msys64"
    "D:\Program Files\msys64"
) do (
    if exist %%P (
        set "MSYS2_ROOT=%%~P"
        call :print_ok "从常见路径找到: !MSYS2_ROOT!"
        goto :msys2_found
    )
)

:: ---- 未找到 MSYS2 ----
call :print_fail "MSYS2 安装目录未找到"
echo.
echo   MSYS2 not found. 请尝试:
echo   1. 从 https://www.msys2.org/ 下载安装 MSYS2
echo   2. 设置环境变量: set MSYS2_ROOT=C:\msys64
echo   3. 重新运行此脚本
echo.
goto :glfw_manual

:msys2_found
echo.

:: ============================================================================
:: 阶段 2：检查 MSYS2 中的 MinGW 子系统
:: ============================================================================
call :separator
echo   [2] 检测可用的 MinGW 子系统
call :separator

set SUBSYS_FOUND=0
for %%S in (mingw64 ucrt64 clang64 mingw32 clang32) do (
    if exist "!MSYS2_ROOT!\%%S\bin\g++.exe" (
        call :print_ok "%%S 子系统可用"
        set /a SUBSYS_FOUND+=1
    )
)
if !SUBSYS_FOUND!==0 (
    call :print_fail "未找到任何 MinGW 子系统 (需要安装工具链)"
    call :print_info "MSYS2 终端中运行: pacman -S mingw-w64-ucrt-x86_64-toolchain"
)

echo.

:: ============================================================================
:: 阶段 3：检查 GLFW
:: ============================================================================
call :separator
echo   [3] 检查 GLFW 窗口库
call :separator

set GLFW_OK=0

for %%S in (mingw64 ucrt64 clang64) do (
    set "INC=!MSYS2_ROOT!\%%S\include\GLFW\glfw3.h"
    set "LIB_A=!MSYS2_ROOT!\%%S\lib\libglfw3.a"
    set "LIB_DLLA=!MSYS2_ROOT!\%%S\lib\libglfw3.dll.a"
    set "DLL=!MSYS2_ROOT!\%%S\bin\glfw3.dll"

    if exist "!INC!" (
        call :print_ok "头文件 (%%S): !INC!"
        set GLFW_OK=1
    ) else (
        call :print_fail "头文件 (%%S): 未找到"
    )

    if exist "!LIB_A!" (
        call :print_ok "静态库 (%%S): !LIB_A!"
        set GLFW_OK=1
    ) else if exist "!LIB_DLLA!" (
        call :print_ok "导入库 (%%S): !LIB_DLLA!"
        set GLFW_OK=1
    ) else (
        call :print_fail "库文件 (%%S): 未找到 libglfw3.a / libglfw3.dll.a"
    )

    if exist "!DLL!" (
        call :print_ok "运行时 (%%S): !DLL!"
    )
)

if !GLFW_OK!==0 (
    echo.
    call :print_fail "GLFW 未安装或未找到"
    echo.
    echo   [修复方法] 在 MSYS2 终端 ^(UCRT64^) 中运行:
    echo     pacman -S mingw-w64-ucrt-x86_64-glfw
    echo.
    echo   或针对特定子系统:
    echo     pacman -S mingw-w64-x86_64-glfw        ^(mingw64^)
    echo     pacman -S mingw-w64-ucrt-x86_64-glfw   ^(ucrt64^)
    echo     pacman -S mingw-w64-clang-x86_64-glfw  ^(clang64^)
    echo.
    echo   如果已通过 pacman 安装但仍未找到:
    echo     1. 确认你使用了正确的子系统
    echo     2. 设置环境变量 MSYS2_ROOT
    echo        set MSYS2_ROOT=!MSYS2_ROOT!
    echo     3. 或在 CMakeLists.txt 中手动指定 GLFW 路径
) else (
    echo.
    call :print_ok "GLFW 检测通过 -- 可正常使用"
)

echo.

:: ============================================================================
:: 阶段 4：检查 GLM
:: ============================================================================
call :separator
echo   [4] 检查 GLM 数学库（纯头文件）
call :separator

set GLM_OK=0

for %%S in (mingw64 ucrt64 clang64) do (
    set "GLM_HPP=!MSYS2_ROOT!\%%S\include\glm\glm.hpp"

    if exist "!GLM_HPP!" (
        call :print_ok "头文件 (%%S): !GLM_HPP!"
        set GLM_OK=1
    ) else (
        call :print_fail "头文件 (%%S): 未找到"
    )
)

if !GLM_OK!==0 (
    echo.
    call :print_fail "GLM 未安装或未找到"
    echo.
    echo   [修复方法] 在 MSYS2 终端 ^(UCRT64^) 中运行:
    echo     pacman -S mingw-w64-ucrt-x86_64-glm
    echo.
    echo   GLM 是纯头文件库, 也可手动下载:
    echo     https://github.com/g-truc/glm/releases
    echo   解压后设置环境变量: set GLM_INCLUDE_DIR=解压目录
) else (
    echo.
    call :print_ok "GLM 检测通过 (纯头文件, 无需链接)"
)

:glfw_manual
echo.

:: ============================================================================
:: 阶段 5：CMake 集成建议
:: ============================================================================
call :separator
echo   [5] CMake 集成方案
call :separator

echo.
echo   本项目的 CMakeLists.txt 已集成 GLFW 三层回退:
echo     1. find_package(glfw3^)
echo     2. FindGLFW_MSYS2.cmake (自动探测 MSYS2^)
echo     3. find_library + find_path (通用回退^)
echo.
echo   GLM 已集成两层回退:
echo     1. find_package(glm^)
echo     2. FindGLM_MSYS2.cmake (自动探测 MSYS2^)
echo.

if not "%MSYS2_ROOT%"=="" (
    echo   MSYS2 根目录: !MSYS2_ROOT!
    echo.
    echo   手动 CMake 配置命令:
    echo     cmake -S . -B build -G "MinGW Makefiles"
    echo         -DCMAKE_CXX_COMPILER=!MSYS2_ROOT!\ucrt64\bin\g++.exe
)

echo ================================================================
echo   诊断完成.
echo ================================================================
echo.

if "%1"=="" pause >nul
endlocal
exit /b 0
