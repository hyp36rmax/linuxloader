@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set "CAPTURE_ROOT=%~dp0AER-DEV5-captures"
set "RUN_GUARD=%CAPTURE_ROOT%\DEV5_SESSION_STARTED.txt"
if exist "%RUN_GUARD%" (
  echo DEV 5 has already been started from this package.
  echo Use a freshly extracted package for the one controlled validation session.
  exit /b 2
)

if not exist "%~dp0Jennifer" (
  echo DEV 5 FAILED: Jennifer was not found beside this launcher.
  exit /b 3
)
if not exist "%~dp0linuxloader.exe" (
  echo DEV 5 FAILED: linuxloader.exe is missing.
  exit /b 4
)

for /f %%I in ('powershell -NoProfile -Command "(Get-FileHash -Algorithm SHA256 -LiteralPath '%~dp0Jennifer').Hash.ToLowerInvariant()"') do set "JENNIFER_SHA256=%%I"
if /i not "%JENNIFER_SHA256%"=="f16fc04d836a2bd8e401d8f987d4fe694fa16e18a9f870da6623d7f884911075" (
  echo DEV 5 FAILED: Jennifer is not the verified DVP-0015A executable.
  exit /b 5
)
for /f "tokens=2,* delims=:" %%A in ('findstr /b /c:"Full commit SHA:" "%~dp0BUILD_INFO.txt"') do set "AER_LOADER_COMMIT=%%B"
for /f "tokens=*" %%I in ("%AER_LOADER_COMMIT%") do set "AER_LOADER_COMMIT=%%I"
if not defined AER_LOADER_COMMIT (
  echo DEV 5 FAILED: BUILD_INFO.txt does not identify the build commit.
  exit /b 6
)

for /f %%I in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd-HHmmss"') do set "STAMP=%%I"
set "SESSION=%CAPTURE_ROOT%\%STAMP%"
mkdir "%SESSION%" >nul 2>&1
if errorlevel 1 (
  echo DEV 5 FAILED: could not create the dedicated capture directory.
  exit /b 7
)
>"%RUN_GUARD%" echo DEV 5 session started: %STAMP%

set "AER_VIRTUAL_DRIVEBOARD=1"
set "AER_VIRTUAL_DRIVEBOARD_COUNT=1"
set "AER_VIRTUAL_DRIVEBOARD_PHYSICAL_PASSTHROUGH=0"
set "AER_GAME_EXECUTABLE_SHA256=f16fc04d836a2bd8e401d8f987d4fe694fa16e18a9f870da6623d7f884911075"
set "AER_CABINET_TYPE=single"
set "AER_CABINET_ID=research-virtual"

set "AER_DRIVEBOARD_RECORDER=1"
set "AER_DRIVEBOARD_OUTPUT=%SESSION%\driveboard_raw"
set "AER_DRIVEBOARD_MARKER_FILE=%SESSION%\driveboard_capture.marker"
set "AER_ACTIVATION_DIAGNOSTICS=1"
set "AER_ACTIVATION_DIAGNOSTICS_OUTPUT=%SESSION%\activation_diagnostics.json"
set "AER_NATIVE_ACTIVATION=1"
set "AER_NATIVE_ACTIVATION_OUTPUT=%SESSION%\native_activation.json"
set "AER_VIRTUAL_DRIVEBOARD_STATUS_OUTPUT=%SESSION%\virtual_driveboard_status.json"

echo DEV 5 starting. Diagnostics will be written to:
echo %SESSION%
linuxloader.exe -g "%~dp0" -c "%~dp0DEV5-virtual-driveboard.ini"
set "LOADER_EXIT=%ERRORLEVEL%"

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Finalize-DEV5-Capture.ps1" -SessionPath "%SESSION%" -LoaderExitCode %LOADER_EXIT%
set "FINALIZE_EXIT=%ERRORLEVEL%"
echo.
if "%FINALIZE_EXIT%"=="0" (
  echo DEV 5 SUCCESS: native activation evidence is complete.
) else (
  echo DEV 5 INCOMPLETE: see DEV5_FAILURE.txt in the capture directory.
)
echo Capture directory: %SESSION%
pause
exit /b %FINALIZE_EXIT%
