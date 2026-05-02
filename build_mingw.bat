@echo off
setlocal
set "VULKAN_SDK=D:\Program Files\VulkanSDK\1.4.341.1"
set "PATH=%VULKAN_SDK%\Bin;%PATH%"
if not exist build mkdir build
cmake -G "MinGW Makefiles" -S . -B build -DCMAKE_BUILD_TYPE=Release -DVULKAN_SDK="%VULKAN_SDK%"
if %ERRORLEVEL% NEQ 0 (
  echo cmake configuration failed
  endlocal
  exit /b %ERRORLEVEL%
)
cmake --build build --config Release -- -j%NUMBER_OF_PROCESSORS%
endlocal
