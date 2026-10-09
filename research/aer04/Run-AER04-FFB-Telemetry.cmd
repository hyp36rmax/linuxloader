@echo off
setlocal EnableExtensions
cd /d "%~dp0"
set "CAPTURE_ROOT=%~dp0AER-04-captures"
if not exist "%~dp0Jennifer" (
    echo AER-04 FAILED: Jennifer missing.
    goto :failed
)
if not exist "%~dp0linuxloader.exe" (
    echo AER-04 FAILED: linuxloader.exe missing.
    goto :failed
)
for /f %%I in ('powershell -NoProfile -Command "(Get-FileHash -Algorithm SHA256 -LiteralPath '%~dp0Jennifer').Hash.ToLowerInvariant()"') do set "JENNIFER_SHA256=%%I"
if /i not "%JENNIFER_SHA256%"=="f16fc04d836a2bd8e401d8f987d4fe694fa16e18a9f870da6623d7f884911075" (
    echo AER-04 FAILED: unverified Jennifer.
    goto :failed
)
if not exist "%~dp0BUILD_INFO.txt" (
    echo AER-04 FAILED: BUILD_INFO.txt missing.
    goto :failed
)
for /f "tokens=1,* delims=:" %%A in ('findstr /b /c:"Full commit SHA:" "%~dp0BUILD_INFO.txt"') do set "AER_LOADER_COMMIT=%%B"
for /f "tokens=*" %%I in ("%AER_LOADER_COMMIT%") do set "AER_LOADER_COMMIT=%%I"
for /f %%I in ('powershell -NoProfile -Command "(Get-Date -Format yyyyMMdd-HHmmss) + '-' + ([guid]::NewGuid().ToString('N').Substring(0,8))"') do set "STAMP=%%I"
set "SESSION=%CAPTURE_ROOT%\%STAMP%"
mkdir "%SESSION%" >nul 2>&1
if errorlevel 1 (
    echo AER-04 FAILED: capture directory unavailable.
    goto :failed
)
set "AER_VIRTUAL_DRIVEBOARD=1"
set "AER_VIRTUAL_DRIVEBOARD_COUNT=2"
set "AER_VIRTUAL_DRIVEBOARD_PHYSICAL_PASSTHROUGH=0"
set "AER_GAME_EXECUTABLE_PATH=%~dp0Jennifer"
set "AER_GAME_EXECUTABLE_SHA256=%JENNIFER_SHA256%"
set "AER_CABINET_TYPE=sdx-two-channel"
set "AER_CABINET_ID=research-virtual"
set "AER_DRIVEBOARD_RECORDER=1"
set "AER_DRIVEBOARD_OUTPUT=%SESSION%\driveboard_raw"
set "AER_ACTIVATION_DIAGNOSTICS=1"
set "AER_ACTIVATION_DIAGNOSTICS_OUTPUT=%SESSION%\activation_diagnostics.json"
set "AER_NATIVE_ACTIVATION=1"
set "AER_NATIVE_ACTIVATION_OUTPUT=%SESSION%\native_activation.json"
set "AER_VIRTUAL_DRIVEBOARD_STATUS_OUTPUT=%SESSION%\virtual_driveboard_status.json"
set "AER_VEHICLE_TELEMETRY=1"
set "AER_VEHICLE_TELEMETRY_OUTPUT=%SESSION%\vehicle_ffb_v1.csv"
echo AER-04 starting. Capture: %SESSION%
linuxloader.exe -g "." -c "AER04-virtual-driveboard.ini"
set "LOADER_EXIT=%ERRORLEVEL%"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Finalize-AER04-Capture.ps1" -SessionPath "%SESSION%" -LoaderExitCode %LOADER_EXIT%
set "FINALIZE_EXIT=%ERRORLEVEL%"
echo Capture directory: %SESSION%
pause
exit /b %FINALIZE_EXIT%
:failed
echo Previous captures remain preserved.
pause
exit /b 1
