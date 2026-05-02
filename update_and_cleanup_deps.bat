@echo off
rem Update GLFW and GLM (if submodules exist pull remote) and remove local backups
setlocal
cd /d %~dp0
rem Try updating as submodules firstngit submodule update --init --remote external/glfw 2>nul || echo Submodule update for glfw skippedngit submodule update --init --remote external/glm 2>nul || echo Submodule update for glm skipped
rem If cloned repos exist, pull latest
if exist external\glfw (
  pushd external\glfw
  git pull --ff-only || echo Failed to pull external\glfw
  popd
)
if exist external\glm (
  pushd external\glm
  git pull --ff-only || echo Failed to pull external\glm
  popd
)
rem Remove local backups if present
if exist external\glfw-local-backup rd /s /q external\glfw-local-backup
if exist external\glm-local-backup rd /s /q external\glm-local-backup
echo Update and cleanup complete.
endlocal
