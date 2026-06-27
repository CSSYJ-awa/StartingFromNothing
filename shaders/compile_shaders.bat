@echo off
REM ============================================================================
REM 着色器编译脚本
REM 需要 Vulkan SDK 提供的 glslc 编译器
REM ============================================================================
echo 编译着色器...

set GLSLC=glslc
if exist "%VULKAN_SDK%\Bin\glslc.exe" (
    set GLSLC="%VULKAN_SDK%\Bin\glslc.exe"
)

echo 使用: %GLSLC%

%GLSLC% vertex.vert -o vertex.spv
if %ERRORLEVEL% neq 0 (
    echo [错误] 顶点着色器编译失败
    exit /b 1
)
echo   vertex.vert -^> vertex.spv [OK]

%GLSLC% fragment.frag -o fragment.spv
if %ERRORLEVEL% neq 0 (
    echo [错误] 片元着色器编译失败
    exit /b 1
)
echo   fragment.frag -^> fragment.spv [OK]

%GLSLC% cull.comp -o cull.spv
if %ERRORLEVEL% neq 0 (
    echo [错误] 剔除着色器编译失败
    exit /b 1
)
echo   cull.comp -^> cull.spv [OK]

echo 着色器编译完成。
