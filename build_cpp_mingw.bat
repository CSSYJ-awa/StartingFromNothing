@echo off
setlocal
if not exist build_cpp mkdir build_cpp
g++ -std=c++17 -O2 -Iexternal\glm -o build_cpp\simple_app.exe src\simple_app.cpp
if exist build_cpp\simple_app.exe (
  echo Running simple_app.exe...
  build_cpp\simple_app.exe
) else (
  echo Build failed: build_cpp\simple_app.exe not found
)
endlocal
exit /b %ERRORLEVEL%
