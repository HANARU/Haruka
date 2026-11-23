@echo off
setlocal

:: ===========================================================================
:: Run a Haruka Python tool script using the bundled embedded Python (E4).
:: Never falls back to system Python; stops with a Setup.bat hint if missing.
:: ===========================================================================

set "ROOT=%~dp0.."
set "PY=%ROOT%\ThirdParty\Python\python.exe"

if not exist "%PY%" (
    echo [Haruka] Embedded Python not found at "%PY%".
    echo [Haruka] Run Setup.bat first to download and configure ThirdParty\Python.
    endlocal & exit /b 1
)

"%PY%" -I %*
set "EXIT_CODE=%errorlevel%"
endlocal & exit /b %EXIT_CODE%
