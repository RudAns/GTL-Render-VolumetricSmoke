@echo off
rem Start the demo as host (listen server). Others on the network can join with RunClient.bat <this machine's address>.
rem The engine is found automatically (see Tools\find_ue.ps1); set UE_ROOT to override.
for /f "usebackq delims=" %%P in (`powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Tools\find_ue.ps1"`) do set UE_ROOT=%%P
if "%UE_ROOT%"=="" (
  echo Unreal Engine 5.8 not found. Set UE_ROOT to the engine folder and run again.
  pause
  exit /b 1
)
start "" "%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor.exe" "%~dp0VolumetricSmoke.uproject" /Game/Maps/Range?listen -game -windowed -ResX=1600 -ResY=900
