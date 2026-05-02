@echo off
rem Quick clone GLFW and GLM into external/ for quick development
setlocal enabledelayedexpansion
cd /d %~dp0
if not exist external mkdir external
if not exist external\glfw (
  git clone --depth 1 https://github.com/glfw/glfw.git external\glfw || (echo Failed cloning GLFW & exit /b 1)
) else (
  echo external\glfw already exists, skipping clone
)
if not exist external\glm (
  git clone --depth 1 https://github.com/g-truc/glm.git external\glm || (echo Failed cloning GLM & exit /b 1)
) else (
  echo external\glm already exists, skipping clone
)
echo Done.
endlocal
