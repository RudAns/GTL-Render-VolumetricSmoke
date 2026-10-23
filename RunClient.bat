@echo off
rem Join a host on this machine (or pass the host machine's address: RunClient.bat <address>).
rem The engine is found automatically (see Tools\find_ue.ps1); set UE_ROOT to override.
for /f "usebackq delims=" %%P in (`powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Tools\find_ue.ps1"`) do set UE_ROOT=%%P
if "%UE_ROOT%"=="" (
  echo Unreal Engine 5.8 not found. Set UE_ROOT to the engine folder and run again.
  pause
  exit /b 1
)
set HOST=%1
if "%HOST%"=="" set HOST=127.0.0.1
start "" "%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor.exe" "%~dp0VolumetricSmoke.uproject" %HOST% -SLJoin=%HOST% -game -windowed -ResX=1600 -ResY=900
