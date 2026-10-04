@echo off
setlocal

:: ===========================================================================
:: ASCII ONLY.  DO NOT put non-ASCII characters in this file.
:: DO NOT call chcp in this file.  See the header of build.bat for why.
:: ===========================================================================

set ROOT=%~dp0
set CONFIG=%~1
if "%CONFIG%"=="" set CONFIG=Release

:: Everything after the configuration is passed to the engine as is,
:: for example "run.bat Release --xr" or "run.bat Release --emulate-stereo".
set ENGINE_ARGS=
shift
:collect_args
if "%~1"=="" goto args_done
set ENGINE_ARGS=%ENGINE_ARGS% %1
shift
goto collect_args
:args_done
set EXE_DIR=%ROOT%build\%CONFIG%
set EXE=%EXE_DIR%\HarukaEngine.exe
set LOGS_DIR=%ROOT%Logs
set LOG=%LOGS_DIR%\latest.txt

if not exist "%EXE%" (
    echo [run] Executable not found: %EXE%
    echo [run] Run build.bat first.
    if not defined HARUKA_NO_PAUSE pause
    endlocal & exit /b 1
)

:: --- 1. Ensure VULKAN_SDK environment is loaded -----------------------------
if "%VULKAN_SDK%"=="" (
    for /f "tokens=2*" %%a in ('reg query "HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment" /v VULKAN_SDK 2^>nul') do set "VULKAN_SDK=%%b"
)
if "%VULKAN_SDK%"=="" (
    for /d %%d in (C:\VulkanSDK\*) do if exist "%%d\Bin\glslc.exe" set "VULKAN_SDK=%%d"
)
if not "%VULKAN_SDK%"=="" (
    set "PATH=%VULKAN_SDK%\Bin;%PATH%"
)

if not exist "%LOGS_DIR%" mkdir "%LOGS_DIR%"

:: A stale log file left locked by a previous crashed run would make
:: Tee-Object fail, so remove it first and ignore any error.
if exist "%LOG%" del /f /q "%LOG%" 2>nul

echo [run] Launching HarukaEngine [%CONFIG%]%ENGINE_ARGS%...
pushd "%EXE_DIR%"

:: Tee-Object shows output on the console and writes it to the log at the same
:: time.  The pipeline's exit code belongs to PowerShell, not to the exe, so
:: "exit $LASTEXITCODE" is required to forward the real exit code.  Without it
:: a crashing exe still reports success.
powershell -NoProfile -ExecutionPolicy Bypass -Command "& '.\HarukaEngine.exe'%ENGINE_ARGS% 2>&1 | Tee-Object -FilePath '%LOG%'; exit $LASTEXITCODE"
set EXIT_CODE=%errorlevel%
popd

echo.
echo [run] Exit code: %EXIT_CODE%
echo [run] Log saved: %LOG%

:: endlocal discards local variables, so every line that reads EXIT_CODE must
:: come before it.  The previous version called endlocal first, which made
:: %EXIT_CODE% expand to nothing: the script paused on every run, even on a
:: clean exit, and always returned 0.  "endlocal & exit /b %VAR%" is the
:: standard idiom because the whole line is expanded before endlocal runs.
if not "%EXIT_CODE%"=="0" if not defined HARUKA_NO_PAUSE pause
endlocal & exit /b %EXIT_CODE%
