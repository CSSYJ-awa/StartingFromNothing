@echo off
REM wrapper to call PowerShell script with positional args (preserves quoting)
set "SCRIPT_DIR=%~dp0"
echo Args: [%~1] [%~2] [%~3] > "%TEMP%\embed_spv_args.log"
powershell -ExecutionPolicy Bypass -File "%SCRIPT_DIR%embed_spv.ps1" -InPath "%~1" -OutPath "%~2" -VarName "%~3" >> "%TEMP%\embed_spv_args.log" 2>&1
type "%TEMP%\embed_spv_args.log"
