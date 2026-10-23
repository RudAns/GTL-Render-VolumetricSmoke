# Build -> (optionally) rebuild assets -> run scripted self-tests. Usage:
#   powershell -File Tools/iterate.ps1 [-Assets] [-NoBuild] [-NoHost] [-Net]
#   -Net also runs a listen server + client pair over 127.0.0.1 and checks synchronisation from the client.
param([switch]$Assets, [switch]$NoBuild, [switch]$NoHost, [switch]$Net, [int]$ResX = 1600, [int]$ResY = 900)
$ErrorActionPreference = "Continue"
$p = Split-Path -Parent $PSScriptRoot
$ue = Join-Path (& "$PSScriptRoot\find_ue.ps1") "Engine"
$proj = "$p\VolumetricSmoke.uproject"
$editor = "$ue\Binaries\Win64\UnrealEditor.exe"

if (-not $NoBuild) {
    $build = & "$ue\Build\BatchFiles\Build.bat" VolumetricSmokeEditor Win64 Development -Project="$proj" -WaitMutex 2>&1
    $build | Select-String -Pattern "error C|error LNK|error :|Result:" | ForEach-Object { $_.Line }
    if (-not ($build | Select-String -Pattern "Result: Succeeded")) { "BUILD FAILED - stopping"; exit 1 }
}
if ($Assets) {
    Get-ChildItem "$p\Content" -Recurse -File -ErrorAction SilentlyContinue | Where-Object { $_.IsReadOnly } | ForEach-Object { $_.IsReadOnly = $false }
    & "$ue\Binaries\Win64\UnrealEditor-Cmd.exe" "$proj" -run=pythonscript -script="$p\Tools\build_assets.py" -unattended -nop4 -nosplash -stdout 2>&1 |
        Select-String -Pattern "build_assets|LogPython: Error|Error:|error X|Failed to compile|Traceback" | ForEach-Object { $_.Line }
}
$out = "$p\Saved\Capture"
New-Item -ItemType Directory -Force $out | Out-Null
function Run-Game($role, $url, $log, $extra) {
    $argList = @("`"$proj`"", $url, "-game", "-windowed", "-ResX=$ResX", "-ResY=$ResY", "-SLCapture=$out", "-SLCaptureRole=$role", "-RenderOffScreen", "-ForceRes",
              "-ExecCmds=`"r.SetRes ${ResX}x${ResY}w`"", "-unattended", "-nosound", "-nosplash", "-log=$log") + $extra
    return Start-Process -FilePath $editor -ArgumentList $argList -PassThru
}
function Show-Log($log) {
    Select-String -Path "$p\Saved\Logs\$log" -Pattern "SLTEST|Error: |error X|Failed to compile|Ensure condition|Fatal|Assertion" | Select-Object -First 60 |
        ForEach-Object { $_.Line -replace '^\[[^\]]*\]\[[^\]]*\]', '' }
}
if (-not $NoHost) {
    Get-ChildItem $out -Filter "*.png" | Where-Object { $_.Name -notlike "c*" } | Remove-Item -Force
    Remove-Item "$out\host_results.txt" -ErrorAction SilentlyContinue
    $proc = Run-Game "host" "/Game/Maps/Range?listen" "host.log" @()
    if (-not $proc.WaitForExit(600000)) { $proc.Kill(); "host capture timeout" }
    Show-Log "host.log"
}
if ($Net) {
    Get-ChildItem $out -Filter "c*.png" | Remove-Item -Force
    Remove-Item "$out\client_results.txt", "$out\server_results.txt" -ErrorAction SilentlyContinue
    $srv = Run-Game "server" "/Game/Maps/Range?listen -port=17791" "server.log" @()
    Start-Sleep -Seconds 16
    $cli = Run-Game "client" "127.0.0.1:17791" "client.log" @()
    if (-not $cli.WaitForExit(400000)) { $cli.Kill(); "client capture timeout" }
    if (-not $srv.HasExited) { $srv.Kill() }
    Show-Log "client.log"
}
Get-ChildItem $out -Filter *.png | ForEach-Object { $_.Name }
