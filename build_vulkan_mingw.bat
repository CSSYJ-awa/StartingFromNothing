@echo off
setlocal
set "VULKAN_SDK=D:\Program Files\VulkanSDK\1.4.341.1"
if not exist build mkdir build
cmake -G "MinGW Makefiles" -S . -B build -DVULKAN_SDK="%VULKAN_SDK%" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug -- -j %NUMBER_OF_PROCESSORS%
if exist build\main.exe (
  echo Running main.exe for 3 seconds...
  start "vulkan_run" /b cmd /c "build\main.exe > build\run_stdout.txt 2> build\run_stderr.txt"
  timeout /t 3 /nobreak >nul
  for /f "tokens=2 delims=," %%i in ('tasklist /FI "IMAGENAME eq main.exe" /FO CSV /NH') do (
    taskkill /PID %%~i /F >nul 2>&1
  )
  type build\run_stderr.txt
) else (
  echo Build failed: build\main.exe not found
)
endlocal
exit /b %ERRORLEVEL%
