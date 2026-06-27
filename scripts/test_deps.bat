@echo off
setlocal enabledelayedexpansion
set "MSYS2_ROOT=D:\Program Files\msys64"
echo --- Phase 1 ---
if exist "%MSYS2_ROOT%\mingw64\include\GLFW\glfw3.h" (
    echo GLFW found
) else (
    echo GLFW not found
)
echo --- Phase 2 ---
if exist "%MSYS2_ROOT%\mingw64\include\glm\glm.hpp" (
    echo GLM found
) else (
    echo GLM not found
)
echo --- Done ---
pause
