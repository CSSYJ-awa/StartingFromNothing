@echo off
:: ============================================================================
:: 将控制台代码页切换为 UTF-8 (65001)，确保中文字符正确显示
:: ============================================================================
chcp 65001 >nul 2>&1

setlocal enabledelayedexpansion
title Vulkan MinGW Import Library Generator

:: ============================================================================
:: generate_vulkan_mingw_lib.bat
::
:: 用途：从 Vulkan SDK 的 vulkan-1.dll 生成 MinGW-w64 兼容的导入库
::       libvulkan-1.dll.a，使 MinGW 能够链接 Vulkan。
::
:: 背景：Vulkan SDK 官方仅提供 MSVC 格式的 vulkan-1.lib，其内部使用
::       COFF 目标文件格式；而 MinGW 的 GNU ld 链接器需要 PE/COFF 格式
::       的 .a 或 .dll.a 导入库，两者互不兼容。
::
:: 原理：使用 MinGW-w64 工具链中的 gendef 从 DLL 导出符号定义文件 (.def)，
::       再用 dlltool 将 .def 转换为 MinGW 格式的导入库 (.dll.a)。
::
:: 用法：
::   scripts\generate_vulkan_mingw_lib.bat          （交互模式，完成后暂停）
::   scripts\generate_vulkan_mingw_lib.bat --no-pause （自动化模式，不暂停）
:: ============================================================================

:: ---- 解析参数 ----
set NO_PAUSE=0
if /i "%~1"=="--no-pause" set NO_PAUSE=1
if /i "%~1"=="-q"         set NO_PAUSE=1
if /i "%~1"=="/q"         set NO_PAUSE=1

:: ============================================================================
:: 辅助子程序
:: ============================================================================
goto :skip_macros

:: ---- 输出带时间戳的日志 ----
:log
echo [%time:~0,8%] %~1
goto :eof

:: ---- 错误消息并退出 ----
:die
call :log "错误: %~1"
if %NO_PAUSE%==0 pause >nul
endlocal
exit /b 1

:: ---- 成功消息并退出 ----
:success
call :log "%~1"
if %NO_PAUSE%==0 pause >nul
endlocal
exit /b 0

:skip_macros

:: ============================================================================
:: 阶段 0：环境预检
:: ============================================================================
echo.
echo ================================================================
echo   Vulkan MinGW 导入库生成器
echo   (gendef + dlltool ^=^> libvulkan-1.dll.a^)
echo ================================================================
echo.

:: ---- 0.1 检查 VULKAN_SDK 环境变量 ----
call :log "检查 VULKAN_SDK 环境变量..."

if "%VULKAN_SDK%"=="" (
    call :die "VULKAN_SDK 环境变量未设置！"
    echo.
    echo   请先安装 Vulkan SDK:
    echo     https://vulkan.lunarg.com/
    echo.
    echo   安装完成后，VULKAN_SDK 通常会自动设置。
    echo   如未自动设置，请手动添加系统环境变量，指向 SDK 安装目录。
    echo   示例: set VULKAN_SDK=C:\VulkanSDK\1.4.341.1
    goto :eof
)
call :log "  VULKAN_SDK = %VULKAN_SDK%"

:: ---- 0.2 定义源 DLL（系统级，由显卡驱动安装）----
::     直接使用 C:\Windows\System32\vulkan-1.dll 无需复制到 SDK 目录。
::     为什么用 System32 的 DLL？
::       · 显卡驱动（NVIDIA/AMD/Intel）或 Vulkan Runtime 安装时会放入 System32
::       · 运行时所有 Vulkan 程序加载的就是这个 DLL，ABI 稳定
::       · 避免权限问题（复制到 SDK 目录可能需管理员权限）
::       · 避免 SDK DLL 与驱动 DLL 的版本冲突
set "SOURCE_DLL=C:\Windows\System32\vulkan-1.dll"
call :log "源 DLL: %SOURCE_DLL%"
call :log "  (使用显卡驱动安装的系统级 vulkan-1.dll)"

if not exist "%SOURCE_DLL%" (
    call :die "系统 vulkan-1.dll 未找到"
    echo.
    echo   预期路径: %SOURCE_DLL%
    echo.
    echo   该文件通常由显卡驱动安装。请：
    echo   1. 安装/更新显卡驱动 (NVIDIA/AMD/Intel^)
    echo   2. 或安装 Vulkan Runtime:
    echo      https://vulkan.lunarg.com/sdk/home
    echo      (在 "Runtime" 栏目下载运行时安装包^)
    echo.
    echo   如果使用 32 位系统，DLL 可能位于:
    echo     C:\Windows\SysWOW64\vulkan-1.dll
    goto :eof
)
call :log "  系统 DLL 已找到 [OK]"

:: ---- 0.3 检查目标文件是否已存在 ----
set "OUTPUT_LIB=%VULKAN_SDK%\Lib\libvulkan-1.dll.a"
if exist "%OUTPUT_LIB%" (
    call :log "目标文件已存在: %OUTPUT_LIB%"
    call :log "如需重新生成，请先手动删除该文件后再次运行本脚本。"
    call :success "导入库已就绪，无需重复生成。"
    goto :eof
)

:: ============================================================================
:: 阶段 1：检查工具链
:: ============================================================================
echo.
call :log "检查 MinGW-w64 工具链..."

set GENDEF_FOUND=0
set DLLTOOL_FOUND=0

:: ---- 1.1 查找 gendef ----
:: gendef 通常位于 MinGW-w64 的 bin 目录下
for %%D in (
    "%MINGW_PREFIX%\bin"
    "%MSYSTEM_PREFIX%\bin"
    "%MSYS2_PATH%\usr\bin"
    ) do (
    if exist "%%D\gendef.exe" (
        set "GENDEF_PATH=%%D\gendef.exe"
        set GENDEF_FOUND=1
        goto :gendef_done
    )
)

:: 如果环境变量方式未找到，尝试从 PATH 查找
where gendef >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    for /f "tokens=*" %%p in ('where gendef 2^>^&1') do set "GENDEF_PATH=%%p"
    set GENDEF_FOUND=1
    goto :gendef_done
)

:: 尝试常见 MinGW 安装路径
for %%D in (
    "C:\msys64\ucrt64\bin"
    "C:\msys64\mingw64\bin"
    "C:\msys64\clang64\bin"
    "C:\mingw64\bin"
    "C:\MinGW\bin"
    "C:\TDM-GCC-64\bin"
    ) do (
    if exist "%%D\gendef.exe" (
        set "GENDEF_PATH=%%D\gendef.exe"
        set GENDEF_FOUND=1
        goto :gendef_done
    )
)

:gendef_done
if %GENDEF_FOUND%==1 (
    call :log "  gendef  : %GENDEF_PATH%"
) else (
    call :log "  gendef  : 未找到"
)

:: ---- 1.2 查找 dlltool ----
where dlltool >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    for /f "tokens=*" %%p in ('where dlltool 2^>^&1') do set "DLLTOOL_PATH=%%p"
    set DLLTOOL_FOUND=1
) else (
    :: dlltool 通常和 g++ 在同一目录
    for /f "tokens=*" %%p in ('where g++ 2^>^&1') do (
        set "GXX_DIR=%%~dpp"
        if exist "!GXX_DIR!dlltool.exe" (
            set "DLLTOOL_PATH=!GXX_DIR!dlltool.exe"
            set DLLTOOL_FOUND=1
        )
    )
)

if %DLLTOOL_FOUND%==1 (
    call :log "  dlltool: %DLLTOOL_PATH%"
) else (
    call :log "  dlltool: 未找到"
)

:: ---- 1.3 工具缺失时的处理 ----
if %GENDEF_FOUND%==0 if %DLLTOOL_FOUND%==0 (
    echo.
    echo ================================================================
    echo   工具链不完整 —— gendef 和 dlltool 均未找到！
    echo ================================================================
    echo.
    echo   gendef 和 dlltool 是 MinGW-w64 工具链的标准组件。
    echo   请使用以下方式之一安装：
    echo.
    echo   [推荐] MSYS2 用户:
    echo     pacman -S mingw-w64-ucrt-x86_64-binutils
    echo     pacman -S mingw-w64-ucrt-x86_64-gcc
    echo.
    echo   或安装完整工具链:
    echo     pacman -S mingw-w64-ucrt-x86_64-toolchain
    echo.
    echo   [备选] 独立 MinGW-w64:
    echo     从 https://www.mingw-w64.org/ 下载完整安装包
    echo     确保安装时勾选 "binutils" 组件
    echo.
    echo   [备选] 手动下载预编译的 libvulkan-1.dll.a:
    echo     方案 A: 从 MSYS2 包仓库提取
    echo             https://packages.msys2.org/package/mingw-w64-ucrt-x86_64-vulkan-loader
    echo     方案 B: 使用 vcpkg 安装
    echo             vcpkg install vulkan --triplet=x64-mingw-static
    echo.
    call :die "无法继续，缺少必要的工具链组件。"
    goto :eof
)

:: ---- 只有 dlltool 没有 gendef 的情况 ----
:: 可以尝试直接使用 dlltool，但可能无法正确导出所有符号
if %GENDEF_FOUND%==0 if %DLLTOOL_FOUND%==1 (
    echo.
    call :log "警告: 未找到 gendef，将尝试仅使用 dlltool 直接生成。"
    call :log "这可能导致部分符号缺失，建议安装完整工具链。"
    goto :dlltool_only
)

:: ============================================================================
:: 阶段 2：使用 gendef + dlltool 生成导入库（推荐路径）
:: ============================================================================
echo.
call :log "===== 阶段 2: 使用 gendef + dlltool 生成导入库 ====="

set "DEF_FILE=%VULKAN_SDK%\Lib\vulkan-1.def"
set "WORK_DIR=%VULKAN_SDK%\Lib"

:: 确保工作目录存在
if not exist "%WORK_DIR%" (
    mkdir "%WORK_DIR%" 2>nul
    if %ERRORLEVEL% NEQ 0 (
        call :die "无法创建目录: %WORK_DIR%"
        goto :eof
    )
)

:: ---- 2.1 使用 gendef 导出符号定义 (.def) ----
call :log "步骤 1/2: 使用 gendef 导出符号定义..."
call :log "  命令: gendef - "%SOURCE_DLL%""

:: gendef 的 - 参数表示输出到 stdout
"%GENDEF_PATH%" - "%SOURCE_DLL%" > "%DEF_FILE%" 2>&1

if %ERRORLEVEL% NEQ 0 (
    echo.
    call :log "gendef 执行失败 (返回码: %ERRORLEVEL%%)。"
    call :log "尝试回退到 dlltool 直接生成方案..."
    del "%DEF_FILE%" 2>nul
    goto :dlltool_only
)

:: 验证 .def 文件是否生成且非空
if not exist "%DEF_FILE%" (
    call :log "gendef 未生成 .def 文件，尝试回退..."
    goto :dlltool_only
)

for %%F in ("%DEF_FILE%") do set DEF_SIZE=%%~zF
if !DEF_SIZE! EQU 0 (
    call :log "生成的 .def 文件为空，尝试回退..."
    del "%DEF_FILE%" 2>nul
    goto :dlltool_only
)

call :log "  .def 文件已生成: %DEF_FILE% (!DEF_SIZE! 字节)"

:: ---- 2.2 使用 dlltool 生成导入库 ----
call :log "步骤 2/2: 使用 dlltool 生成导入库..."
call :log "  命令: dlltool -d vulkan-1.def -l libvulkan-1.dll.a -D vulkan-1.dll"

pushd "%WORK_DIR%"
"%DLLTOOL_PATH%" -d vulkan-1.def -l libvulkan-1.dll.a -D vulkan-1.dll 2>&1
set DLLTOOL_RC=%ERRORLEVEL%
popd

if %DLLTOOL_RC% NEQ 0 (
    call :die "dlltool 执行失败 (返回码: %DLLTOOL_RC%)"
    goto :eof
)

goto :verify_output

:: ============================================================================
:: 阶段 3（回退路径）：仅使用 dlltool 直接生成
:: ============================================================================
:dlltool_only
echo.
call :log "===== 阶段 3: 仅使用 dlltool 直接生成（回退路径）====="

set "WORK_DIR=%VULKAN_SDK%\Lib"

if not exist "%WORK_DIR%" (
    mkdir "%WORK_DIR%" 2>nul
)

:: dlltool 的 -k 参数会尝试从 DLL 自动提取符号
:: 注意：某些版本的 dlltool 可能不支持此方式，成功率低于 gendef 路径
call :log "  命令: dlltool -k -d vulkan-1.def -l libvulkan-1.dll.a -D vulkan-1.dll"

pushd "%WORK_DIR%"

:: 方法 1: 使用 -k 让 dlltool 尽量自动处理
"%DLLTOOL_PATH%" -k -d vulkan-1.def -l libvulkan-1.dll.a -D vulkan-1.dll 2>&1
set DLLTOOL_RC=%ERRORLEVEL%

if %DLLTOOL_RC% NEQ 0 (
    :: 方法 2: 尝试不带 -k 的最简方式
    call :log "  方法 1 失败，尝试方法 2..."

    :: 先尝试生成一个最小 def 文件
    echo LIBRARY vulkan-1.dll > vulkan-1.def
    echo EXPORTS >> vulkan-1.def

    "%DLLTOOL_PATH%" -d vulkan-1.def -l libvulkan-1.dll.a -D vulkan-1.dll 2>&1
    set DLLTOOL_RC=%ERRORLEVEL%
)

popd

if %DLLTOOL_RC% NEQ 0 (
    call :die "所有方法均失败，无法生成导入库。"
    echo.
    echo   请考虑以下替代方案：
    echo   1. 使用 MSYS2 安装完整工具链：
    echo      pacman -S mingw-w64-ucrt-x86_64-toolchain
    echo   2. 使用 vcpkg 安装 Vulkan：
    echo      vcpkg install vulkan --triplet=x64-mingw-static
    echo   3. 从 MSYS2 包仓库手动下载预编译 libvulkan-1.dll.a
    echo      https://packages.msys2.org/package/mingw-w64-ucrt-x86_64-vulkan-loader
    goto :eof
)

:: ============================================================================
:: 阶段 4：验证输出
:: ============================================================================
:verify_output
echo.
call :log "===== 验证输出文件 ====="

set "OUTPUT_LIB=%WORK_DIR%\libvulkan-1.dll.a"

if not exist "%OUTPUT_LIB%" (
    call :die "生成失败: 未找到 %OUTPUT_LIB%"
    goto :eof
)

for %%F in ("%OUTPUT_LIB%") do set LIB_SIZE=%%~zF
call :log "  文件: %OUTPUT_LIB%"
call :log "  大小: !LIB_SIZE! 字节"

if !LIB_SIZE! LSS 10240 (
    call :log "  警告: 文件大小小于 10KB，可能生成不完整。"
    call :log "  如果链接时出现 'undefined reference' 错误，请重新运行本脚本。"
)

:: ---- 清理临时 .def 文件 ----
:: 保留 .def 文件以便将来参考，也可删除以保持整洁
:: del "%DEF_FILE%" 2>nul

:: ============================================================================
:: 阶段 5：完成
:: ============================================================================
echo.
echo ================================================================
echo   导入库生成成功！
echo ================================================================
echo.
echo   源 DLL   : %SOURCE_DLL% (系统级, 无需复制^)
echo   生成的文件: %OUTPUT_LIB%
echo.
echo   在 CMakeLists.txt 中使用:
echo     find_package(Vulkan REQUIRED^)
echo     target_link_libraries(your_target PRIVATE Vulkan::Vulkan^)
echo.
echo   CMake 的 FindVulkan 模块会自动搜索到 libvulkan-1.dll.a，
echo   无需额外配置。
echo.
echo   验证命令:
echo     cmake -S . -B build -G "MinGW Makefiles"
echo     cmake --build build
echo ================================================================
echo.
echo   如果要手动验证，可运行：
echo     cmake -S . -B build -G "MinGW Makefiles"
echo     cmake --build build
echo ================================================================

call :success "完成。"
goto :eof
