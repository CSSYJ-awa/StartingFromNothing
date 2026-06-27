@echo off
:: ============================================================================
:: 将控制台代码页切换为 UTF-8 (65001)，确保中文字符正确显示
:: 否则 Windows CMD 默认使用 ANSI 代码页（中文系统为 GBK/CP936），
:: 会导致 UTF-8 编码的 .bat 文件中所有中文变成乱码。
:: ============================================================================
chcp 65001 >nul 2>&1

setlocal enabledelayedexpansion
title VulkanApp - Environment Check

:: ============================================================================
:: check_env.bat -- Windows native environment diagnostic script
:: Usage: scripts\check_env.bat [--no-pause]
:: Checks: CMake / MinGW-w64 / Vulkan SDK / GLFW / GLM
::
:: NOTE: VULKAN_SDK must point to SDK root dir (vulkan-1.dll lives in Bin\)
::       GLFW detection tries both glfw3 and glfw library names
::       (MinGW prebuilt packages may use libglfw3.a or libglfw.a)
:: ============================================================================

:: ---- Color definitions (ANSI escape sequences) ----
:: Get ESC character (ASCII 27 = 0x1B) dynamically

:: 获取 ESC 字符 (ASCII 27 = 0x1B)
for /f %%e in ('echo prompt $E ^| cmd') do set "ESC=%%e"

set "GREEN=%ESC%[92m"
set "RED=%ESC%[91m"
set "YELLOW=%ESC%[93m"
set "CYAN=%ESC%[96m"
set "RESET=%ESC%[0m"

:: Check ANSI support (Windows 10 build 1511+)
:: 低于 Windows 10 的终端不支持 ANSI 转义序列，将降级为纯文本输出
for /f "tokens=4-5 delims=. " %%i in ('ver') do set WIN_VER=%%i.%%j
set ANSI_OK=0
if %WIN_VER% GEQ 10.0 set ANSI_OK=1

:: ============================================================================
:: 辅助子程序
:: ============================================================================

:: 绿色 [OK]
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

:: ---- 计数器 ----
set PASS=0
set FAIL=0

:: ============================================================================
:: 标题
:: ============================================================================
echo.
echo ========================================================
echo        VulkanApp 开发环境诊断 (Windows Batch^)
echo ========================================================
echo.

:: ============================================================================
:: [0] CMake
:: ============================================================================
call :separator
echo   [0] CMake
call :separator

where cmake >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    :: 提取版本号
    for /f "tokens=3" %%v in ('cmake --version 2^>^&1 ^| findstr /c:"cmake version"') do (
        set CMAKE_VER=%%v
    )
    call :print_ok "CMake 已安装: v!CMAKE_VER!"
    set /a PASS+=1
) else (
    call :print_fail "CMake 未安装或不在 PATH 中"
    call :print_info "下载: https://cmake.org/download/"
    set /a FAIL+=1
    goto :skip_cmake
)

:: 版本检查
for /f "tokens=1-3 delims=." %%a in ("!CMAKE_VER!") do (
    set MAJOR=%%a
    set MINOR=%%b
)
if !MAJOR! GTR 3 (
    call :print_ok "CMake 版本 >= 3.15"
    set /a PASS+=1
) else if !MAJOR! EQU 3 if !MINOR! GEQ 15 (
    call :print_ok "CMake 版本 >= 3.15"
    set /a PASS+=1
) else (
    call :print_fail "CMake 版本过低: v!CMAKE_VER! (需要 >= 3.15)"
    set /a FAIL+=1
)
:skip_cmake

:: ============================================================================
:: [1] C++ 编译器 (MinGW-w64)
:: ============================================================================
echo.
call :separator
echo   [1] C++ 编译器 (MinGW-w64^)
call :separator

where g++ >nul 2>&1
if !ERRORLEVEL! EQU 0 (
    for /f "tokens=*" %%g in ('where g++ 2^>^&1') do set GXX_PATH=%%g
    call :print_ok "g++ 路径: !GXX_PATH!"

    :: 使用 -dumpfullversion 直接获取版本号（如 14.2.0），
    :: 避免解析 --version 的多行多token输出在不同MSYS2/MinGW版本下格式不一致。
    for /f "tokens=*" %%v in ('g++ -dumpfullversion 2^>^&1') do set GXX_VER=%%v
    call :print_info "GCC 版本: !GXX_VER!"

    :: 提取主版本号（小数点前部分）
    for /f "tokens=1 delims=." %%a in ("!GXX_VER!") do set GXX_MAJOR=%%a
    if !GXX_MAJOR! GEQ 8 (
        call :print_ok "GCC 版本 >= 8.0 (满足 C++17 要求)"
        set /a PASS+=1
    ) else (
        call :print_fail "GCC 版本过低: !GXX_VER! (需要 >= 8.0)"
        set /a FAIL+=1
    )
) else (
    call :print_fail "g++ 未找到 (MinGW-w64 未安装或不在 PATH 中)"
    call :print_info "下载 MinGW-w64: https://www.mingw-w64.org/"
    call :print_info "或 TDM-GCC: https://jmeubank.github.io/tdm-gcc/"
    set /a FAIL+=1
)

:: 也检查 gcc
where gcc >nul 2>&1
if !ERRORLEVEL! EQU 0 (
    for /f "tokens=*" %%g in ('where gcc 2^>^&1') do set GCC_PATH=%%g
    call :print_ok "gcc 路径: !GCC_PATH!"
    set /a PASS+=1
) else (
    call :print_fail "gcc 未找到"
    set /a FAIL+=1
)

:: ============================================================================
:: [2] Vulkan SDK
:: ============================================================================
echo.
call :separator
echo   [2] Vulkan SDK
call :separator

if "%VULKAN_SDK%"=="" (
    call :print_fail "VULKAN_SDK 环境变量未设置"
    call :print_info "下载 Vulkan SDK: https://vulkan.lunarg.com/"
    call :print_info "安装后 VULKAN_SDK 通常会被自动设置"
    set /a FAIL+=1
    goto :skip_vulkan
) else (
    call :print_ok "VULKAN_SDK = %VULKAN_SDK%"
    set /a PASS+=1
)

:: 头文件
if exist "%VULKAN_SDK%\Include\vulkan\vulkan.h" (
    call :print_ok "vulkan/vulkan.h"
    set /a PASS+=1
) else (
    call :print_fail "缺少: %VULKAN_SDK%\Include\vulkan\vulkan.h"
    set /a FAIL+=1
)

:: 导入库 / 静态库
:: MinGW 需要 .dll.a 格式；MSVC 的 .lib 不兼容
if exist "%VULKAN_SDK%\Lib\libvulkan-1.dll.a" (
    for %%F in ("%VULKAN_SDK%\Lib\libvulkan-1.dll.a") do set VULKAN_LIB_SIZE=%%~zF
    call :print_ok "导入库: libvulkan-1.dll.a (!VULKAN_LIB_SIZE! 字节)"
    set /a PASS+=1
) else if exist "%VULKAN_SDK%\Lib\libvulkan.a" (
    call :print_ok "静态库: libvulkan.a"
    set /a PASS+=1
) else if exist "%VULKAN_SDK%\Lib\vulkan-1.lib" (
    call :print_warn "仅有 MSVC 导入库 (vulkan-1.lib)，MinGW 无法链接"
    call :print_info "解决: 运行 scripts\\generate_vulkan_mingw_lib.bat"
    call :print_info "  gendef + dlltool 会从 vulkan-1.dll 生成 libvulkan-1.dll.a"
    set /a FAIL+=1
) else (
    call :print_fail "缺少 Vulkan 库文件"
    call :print_info "请确认 Vulkan SDK 安装完整"
    set /a FAIL+=1
)

:: 运行时 DLL -- 检查系统级 vulkan-1.dll (显卡驱动安装, C:\Windows\System32)
:: 这是运行时实际加载的 DLL, 比 SDK Bin 下的副本更可靠
if exist "C:\Windows\System32\vulkan-1.dll" (
    call :print_ok "vulkan-1.dll (System32^)"
) else (
    call :print_fail "缺少: C:\Windows\System32\vulkan-1.dll"
    call :print_info "请安装显卡驱动或 Vulkan Runtime: https://vulkan.lunarg.com/"
    set /a FAIL+=1
)

:: MinGW toolchain check (gendef/dlltool for generating .dll.a)
:: Only check when .dll.a is missing AND system DLL exists
if not exist "%VULKAN_SDK%\Lib\libvulkan-1.dll.a" (
    if exist "C:\Windows\System32\vulkan-1.dll" (
        echo.
        echo   -- MinGW 导入库生成工具 --
        where gendef >nul 2>&1
        if !ERRORLEVEL! EQU 0 (
            for /f "tokens=*" %%p in ('where gendef 2^>^&1') do call :print_ok "gendef: %%p"
            set /a PASS+=1
        ) else (
            call :print_warn "gendef 未找到 (自动生成导入库将不可用)"
            set /a FAIL+=1
        )
        where dlltool >nul 2>&1
        if !ERRORLEVEL! EQU 0 (
            for /f "tokens=*" %%p in ('where dlltool 2^>^&1') do call :print_ok "dlltool: %%p"
            set /a PASS+=1
        ) else (
            call :print_warn "dlltool 未找到 (自动生成导入库将不可用)"
            set /a FAIL+=1
        )
        call :print_info "安装工具链: pacman -S mingw-w64-ucrt-x86_64-binutils"
        call :print_info "或运行: scripts\\generate_vulkan_mingw_lib.bat"
    )
)

:skip_vulkan

:: ============================================================================
:: [3] GLFW
:: ============================================================================
echo.
call :separator
echo   [3] GLFW 窗口库
call :separator

set GLFW_FOUND=0

:: ---- 3.1 自动探测 MSYS2 根目录 ----
::     Auto-detect MSYS2 root from g++ path, search all subsystems (ucrt64/mingw64/clang64)
set "MSYS2_CHECK_ROOT="
for /f "delims=" %%p in ('where g++ 2^>^&1') do set "GXX_PATH_GLFW=%%p" & goto :glfw_got_gxx
goto :glfw_no_msys2

:glfw_got_gxx
:: Navigate up: bin -> subsystem -> MSYS2 root
for %%D in ("!GXX_PATH_GLFW!") do set "GLFW_BIN_PARENT=%%~dpD"
if "!GLFW_BIN_PARENT:~-1!"=="\" set "GLFW_BIN_PARENT=!GLFW_BIN_PARENT:~0,-1!"
for %%D in ("!GLFW_BIN_PARENT!") do set "GLFW_SUBSYS_PARENT=%%~dpD"
if "!GLFW_SUBSYS_PARENT:~-1!"=="\" set "GLFW_SUBSYS_PARENT=!GLFW_SUBSYS_PARENT:~0,-1!"
for %%D in ("!GLFW_SUBSYS_PARENT!") do set "GLFW_ROOT_PARENT=%%~dpD"
if "!GLFW_ROOT_PARENT:~-1!"=="\" set "GLFW_ROOT_PARENT=!GLFW_ROOT_PARENT:~0,-1!"

:: 验证是否为 MSYS2 目录 (pacman.conf 是最可靠的特征)
if exist "!GLFW_ROOT_PARENT!\etc\pacman.conf" set "MSYS2_CHECK_ROOT=!GLFW_ROOT_PARENT!"

:glfw_no_msys2

:: ---- 3.2 搜索 GLFW 头文件 ----
:: Search MSYS2 subsystems first, then fallback to env vars and manual paths
if not "%MSYS2_CHECK_ROOT%"=="" (
    for %%S in (ucrt64 mingw64 clang64) do (
        if exist "!MSYS2_CHECK_ROOT!\%%S\include\GLFW\glfw3.h" (
            call :print_ok "GLFW 头文件 (%%S^): !MSYS2_CHECK_ROOT!\%%S\include\GLFW\glfw3.h"
            set GLFW_HEADER_DIR=!MSYS2_CHECK_ROOT!\%%S\include
            set GLFW_LIB_DIR=!MSYS2_CHECK_ROOT!\%%S\lib
            set GLFW_FOUND=1
            set /a PASS+=1
            goto :glfw_lib_search_msys2
        )
    )
    call :print_fail "MSYS2 各子系统下未找到 GLFW 头文件"
    call :print_info "安装: pacman -S mingw-w64-ucrt-x86_64-glfw"
) else (
    call :print_info "未检测到 MSYS2，尝试其他路径..."
)

:: 回退: 环境变量和手动路径
for %%D in (
    "%GLFW_ROOT%\include"
    "%GLFW_DIR%\include"
    "C:\glfw\include"
) do (
    if exist "%%D\GLFW\glfw3.h" (
        call :print_ok "GLFW 头文件: %%D\GLFW\glfw3.h"
        set GLFW_HEADER_DIR=%%D
        set GLFW_FOUND=1
        set /a PASS+=1
        goto :glfw_lib_search_manual
    )
)

if !GLFW_FOUND! EQU 0 (
    call :print_fail "未找到 GLFW/glfw3.h"
    set /a FAIL+=1
    goto :glfw_done
)

:: ---- 3.3 搜索 GLFW 库文件 (MSYS2 路径) ----
:glfw_lib_search_msys2
set GLFW_LIB_FOUND=0
for %%L in (libglfw3.a libglfw3.dll.a glfw3.dll.a) do (
    if exist "!GLFW_LIB_DIR!\%%L" (
        call :print_ok "GLFW 库文件: !GLFW_LIB_DIR!\%%L"
        set GLFW_LIB_FOUND=1
        set /a PASS+=1
        goto :glfw_done
    )
)
call :print_fail "GLFW 库文件未找到 (搜索目录: !GLFW_LIB_DIR!^)"
call :print_info "安装: pacman -S mingw-w64-ucrt-x86_64-glfw"
set /a FAIL+=1
goto :glfw_done

:: ---- 3.3 搜索 GLFW 库文件 (手动路径) ----
:glfw_lib_search_manual
set GLFW_LIB_FOUND=0
for %%D in (
    "%GLFW_ROOT%\lib-mingw-w64"
    "%GLFW_ROOT%\lib"
    "%GLFW_DIR%\lib-mingw-w64"
    "%GLFW_DIR%\lib"
    "C:\glfw\lib-mingw-w64"
    "C:\glfw\lib"
) do (
    for %%L in (libglfw3.a libglfw3.dll.a libglfw.a glfw3.dll.a) do (
        if exist "%%D\%%L" (
            call :print_ok "GLFW 库文件: %%D\%%L"
            set GLFW_LIB_FOUND=1
            set /a PASS+=1
            goto :glfw_done
        )
    )
)

if !GLFW_LIB_FOUND! EQU 0 (
    call :print_fail "未找到 GLFW 库文件 (尝试了 libglfw3.a / glfw3.dll.a / libglfw.a^)"
    call :print_info "修复方案:"
    call :print_info "  A) 设置 GLFW_ROOT 环境变量指向 GLFW 安装目录"
    call :print_info "  B) MSYS2: pacman -S mingw-w64-ucrt-x86_64-glfw"
    call :print_info "  C) 下载: https://www.glfw.org/download.html"
    set /a FAIL+=1
)

:glfw_done

:: ============================================================================
:: [4] GLM
:: ============================================================================
echo.
call :separator
echo   [4] GLM 数学库 (纯头文件^)
call :separator

set GLM_FOUND=0

:: ---- 4.1 MSYS2 subsystem search (reuse root detected in GLFW section) ----
if not "%MSYS2_CHECK_ROOT%"=="" (
    for %%S in (ucrt64 mingw64 clang64) do (
        if exist "!MSYS2_CHECK_ROOT!\%%S\include\glm\glm.hpp" (
            call :print_ok "GLM (%%S^): !MSYS2_CHECK_ROOT!\%%S\include\glm\glm.hpp"
            call :print_info "GLM 是纯头文件库，无需编译"
            set GLM_FOUND=1
            set /a PASS+=1
            goto :glm_done
        )
    )
    call :print_info "MSYS2 各子系统下未找到 GLM"
    call :print_info "安装: pacman -S mingw-w64-ucrt-x86_64-glm"
)

:: ---- 4.2 回退: 环境变量和手动路径 ----
for %%D in (
    "%GLM_ROOT%\include"
    "%GLM_ROOT%"
    "%GLM_DIR%"
    "%GLM_INCLUDE_DIR%"
    "C:\glm"
) do (
    if exist "%%D\glm\glm.hpp" (
        call :print_ok "GLM: %%D\glm\glm.hpp"
        call :print_info "GLM 是纯头文件库，无需编译"
        set GLM_FOUND=1
        set /a PASS+=1
        goto :glm_done
    )
)

if !GLM_FOUND! EQU 0 (
    call :print_fail "未找到 glm/glm.hpp"
    call :print_info "修复方案:"
    call :print_info "  A) MSYS2: pacman -S mingw-w64-ucrt-x86_64-glm"
    call :print_info "  B) 下载: https://github.com/g-truc/glm/releases"
    call :print_info "  C) 将 glm 文件夹复制到项目 include/ 目录下"
    set /a FAIL+=1
)

:glm_done

:: ============================================================================
:: Summary
:: ============================================================================
echo.
echo ========================================================
echo         Environment Check Results
echo ========================================================

set /a TOTAL=%PASS%+%FAIL%
echo   Total : %TOTAL%
echo   Pass  : %PASS%

if %FAIL% GTR 0 (
    echo   Fail  : %FAIL%  ^<-- fix with suggestions above
    echo.
    echo   ############################################
    echo   #  Some checks failed, build may not work. #
    echo   ############################################
) else (
    echo   Fail  : 0
    echo.
    echo   ############################################
    echo   #  All checks passed! Ready to build.      #
    echo   ############################################
)

echo.
echo   Next steps:
echo     cmake -S . -B build -G "MinGW Makefiles"
echo     cmake --build build
echo.

:: Pause if interactive
if "%1"=="" (
    echo Press any key to exit...
    pause >nul
)

endlocal
exit /b %FAIL%
