@echo off
setlocal enabledelayedexpansion

:: ===========================================================================
:: ASCII ONLY.  DO NOT put non-ASCII characters in this file.
:: DO NOT call chcp in this file.
::
:: WHY: cmd.exe reads a batch file by byte offset, decoding it with whatever
:: console codepage is active. A UTF-8 multi-byte character (for example the
:: box-drawing dash U+2500, bytes E2 94 80) is mis-decoded under CP949 and the
:: parser loses the byte boundary. Every line after that point can be split in
:: the wrong place. Calling chcp mid-file makes it worse because the codepage
:: changes while the file is still being parsed.
::
:: This file was in exactly that state: "set ERR_LOG=..." and the cmake
:: invocation were cut in half, %ROOT% expanded to nothing, and CMake was
:: never actually executed.  Korean comments broke run.bat the same way.
:: Keep this file 7-bit clean.
:: ===========================================================================

:: Force English locale for MSVC compiler, MSBuild and .NET
set VSLANG=1033
set PreferredUILang=en-US
set DOTNET_CLI_UI_LANGUAGE=en-US
set LC_ALL=C
set LANG=en_US.UTF-8

set ROOT=%~dp0
set BUILD_DIR=%ROOT%build
set CONFIG=%~1
if "%CONFIG%"=="" set CONFIG=Release
if /i "%CONFIG%"=="clean" (
    set CONFIG=Release
    set DO_CLEAN=1
)
if /i "%~2"=="clean" set DO_CLEAN=1

:: Build logs go under Logs\ so the project root stays clean (no stray files).
if not exist "%ROOT%Logs" mkdir "%ROOT%Logs"
set LOG=%ROOT%Logs\build_log.txt
set ERR_LOG=%ROOT%Logs\build_error.txt

:: Clear previous error log
if exist "%ERR_LOG%" del /f /q "%ERR_LOG%"

:: --- 1. Ensure VULKAN_SDK environment is loaded in current session ----------
if "%VULKAN_SDK%"=="" (
    for /f "tokens=2*" %%a in ('reg query "HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment" /v VULKAN_SDK 2^>nul') do set "VULKAN_SDK=%%b"
)
if "%VULKAN_SDK%"=="" (
    for /f "tokens=2*" %%a in ('reg query "HKCU\Environment" /v VULKAN_SDK 2^>nul') do set "VULKAN_SDK=%%b"
)
if "%VULKAN_SDK%"=="" (
    for /d %%d in (C:\VulkanSDK\*) do if exist "%%d\Bin\glslc.exe" set "VULKAN_SDK=%%d"
)
if not "%VULKAN_SDK%"=="" (
    set "PATH=%VULKAN_SDK%\Bin;%PATH%"
    echo [build] Using Vulkan SDK: %VULKAN_SDK%
) else (
    echo [build] WARNING: VULKAN_SDK not found. CMake may fail to find Vulkan.
)

:: --- 2. Clean cache if requested --------------------------------------------
if "%DO_CLEAN%"=="1" (
    echo [build] Cleaning existing CMake cache...
    if exist "%BUILD_DIR%\CMakeCache.txt" del /f /q "%BUILD_DIR%\CMakeCache.txt"
    if exist "%BUILD_DIR%\CMakeFiles" rmdir /s /q "%BUILD_DIR%\CMakeFiles"
)

:: --- 3. Detect Visual Studio Generator --------------------------------------
set VS_GEN=Visual Studio 18 2026
if not exist "C:\Program Files\Microsoft Visual Studio\18" (
    if exist "C:\Program Files\Microsoft Visual Studio\2022" (
        set VS_GEN=Visual Studio 17 2022
    )
)
echo [build] Target Generator: %VS_GEN%

:: --- 4. Configure CMake -----------------------------------------------------
if not exist "%BUILD_DIR%\CMakeCache.txt" (
    echo [build] Configuring CMake...
    cmake -S "%ROOT%." -B "%BUILD_DIR%" -G "%VS_GEN%" -A x64
    if errorlevel 1 goto fail
)

:: --- 5. Build ---------------------------------------------------------------
echo [build] Building %CONFIG%... (log: %LOG%)
cmake --build "%BUILD_DIR%" --config %CONFIG% --parallel %NUMBER_OF_PROCESSORS% -- /p:PreferredUILang=en-US > "%LOG%" 2>&1
if errorlevel 1 goto fail

:: Extract errors/warnings if any into error log or note success
findstr /i "error fatal" "%LOG%" > "%ERR_LOG%" 2>nul
if %ERRORLEVEL% neq 0 (
    echo No build errors found. > "%ERR_LOG%"
)

echo.
echo [build] Done: %BUILD_DIR%\%CONFIG%\HarukaEngine.exe
:: Set HARUKA_NO_PAUSE=1 to skip the pause when calling this from a script.
if not defined HARUKA_NO_PAUSE pause
endlocal & exit /b 0

:fail
echo.
echo [build] FAILED - full log: %LOG%
findstr /i "error fatal" "%LOG%" > "%ERR_LOG%" 2>nul
type "%LOG%" | findstr /i "error fatal"
if not defined HARUKA_NO_PAUSE pause
endlocal & exit /b 1
