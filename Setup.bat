@echo off
setlocal

:: ===========================================================================
:: ASCII ONLY. DO NOT put non-ASCII characters or call chcp in this file.
:: See build.bat for why cmd.exe byte-offset parsing breaks on UTF-8/chcp.
:: ===========================================================================

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"

set "UNATTENDED=0"
for %%a in (%*) do (
    if /i "%%~a"=="--unattended" set "UNATTENDED=1"
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\Tools\Setup\ensure_python.ps1" -RepoRoot "%ROOT%"
if errorlevel 1 (
    echo [Setup] FAILED at embedded Python bootstrap.
    if "%UNATTENDED%"=="0" pause
    endlocal & exit /b 1
)

"%ROOT%\ThirdParty\Python\python.exe" -I "%ROOT%\Tools\Setup\setup.py" %*
set "EXIT_CODE=%errorlevel%"

if not "%EXIT_CODE%"=="0" (
    if "%UNATTENDED%"=="0" pause
)
endlocal & exit /b %EXIT_CODE%
