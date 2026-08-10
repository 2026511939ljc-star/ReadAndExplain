# Builds the ReadAllandExplains plugin in an isolated host project.
#
# Why this script exists:
#   The installed (Rocket) UE 5.7 ships a precompiled UE5Rules.dll generated on
#   2026-05-07. HoudiniEngine was installed into Engine\Plugins\Runtime on
#   2026-06-29, so its ModuleRules types were never compiled into that assembly.
#   An installed engine refuses to rebuild the assembly, so any build that walks
#   the UnrealEditor target fails with:
#       Expecting to find a type to be declared in a module rules named 'HoudiniEngine'
#   This is unrelated to our source code.
#
#   Workaround: move the Houdini plugins out of the engine for the duration of the
#   build, then always move them back. The restore runs in a finally block so it
#   happens even on failure or Ctrl+C.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File Scripts\BuildPluginIsolated.ps1
#   powershell -ExecutionPolicy Bypass -File Scripts\BuildPluginIsolated.ps1 -KeepOutput

[CmdletBinding()]
param(
    [string] $EngineRoot = 'C:\Program Files\Epic Games\UE_5.7',
    [string] $PluginPath,
    [string] $OutputRoot = 'C:\tmp\rae_cp7_build',
    [switch] $KeepOutput
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($PluginPath)) {
    $PluginPath = Join-Path $PSScriptRoot '..\ReadAllandExplains.uplugin'
}

$runUat = Join-Path $EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat'
$runtimeDir = Join-Path $EngineRoot 'Engine\Plugins\Runtime'
$parkingDir = Join-Path $env:TEMP ('rae_houdini_parked_' + (Get-Date -Format 'yyyyMMdd_HHmmss'))
$houdiniPlugins = @('HoudiniEngine', 'HoudiniLiveLink', 'HoudiniNiagara')

if (-not (Test-Path $runUat)) { throw "RunUAT.bat not found at $runUat" }
$PluginPath = (Resolve-Path $PluginPath).Path
if (-not (Test-Path $PluginPath)) { throw "Plugin descriptor not found at $PluginPath" }

# Refuse to run while an editor holds the plugin DLL, otherwise the link step
# fails for reasons that look like code errors but are not.
$busy = Get-Process -Name 'UnrealEditor', 'UnrealEditor-Cmd' -ErrorAction SilentlyContinue
if ($busy) {
    Write-Host 'WARNING: UE editor processes are running:' -ForegroundColor Yellow
    $busy | ForEach-Object { Write-Host ("  PID {0}  {1}" -f $_.Id, $_.Name) -ForegroundColor Yellow }
    Write-Host 'A concurrent editor can lock the output DLL. Close it if the build fails to link.' -ForegroundColor Yellow
}

$moved = @()
try {
    New-Item -ItemType Directory -Path $parkingDir -Force | Out-Null

    foreach ($name in $houdiniPlugins) {
        $src = Join-Path $runtimeDir $name
        if (Test-Path $src) {
            $dst = Join-Path $parkingDir $name
            Move-Item -LiteralPath $src -Destination $dst -Force
            $moved += [pscustomobject]@{ Name = $name; Source = $src; Parked = $dst }
            Write-Host "  parked  $name" -ForegroundColor DarkGray
        }
    }

    $remaining = @(Get-ChildItem (Join-Path $EngineRoot 'Engine\Plugins') -Recurse -Filter 'Houdini*.Build.cs' -ErrorAction SilentlyContinue)
    if ($remaining.Count -gt 0) {
        Write-Host "WARNING: $($remaining.Count) Houdini Build.cs still present; the build may fail." -ForegroundColor Yellow
        $remaining | ForEach-Object { Write-Host "  $($_.FullName)" -ForegroundColor DarkYellow }
    }

    if (Test-Path $OutputRoot) { Remove-Item $OutputRoot -Recurse -Force }
    $packageDir = Join-Path $OutputRoot 'out'

    Write-Host ''
    Write-Host 'Building plugin (this takes about a minute)...' -ForegroundColor Cyan
    & $runUat BuildPlugin -Plugin="$PluginPath" -Package="$packageDir" -TargetPlatforms=Win64
    $uatExit = $LASTEXITCODE

    # BuildPlugin can report success while skipping compilation entirely, so the
    # binary is the only trustworthy signal.
    $dll = Join-Path $packageDir 'Binaries\Win64\UnrealEditor-ReadAllandExplains.dll'
    $dllExists = Test-Path $dll

    Write-Host ''
    if ($uatExit -eq 0 -and $dllExists) {
        $info = Get-Item $dll
        Write-Host 'BUILD VERIFIED' -ForegroundColor Green
        Write-Host ("  dll     {0}" -f $dll)
        Write-Host ("  bytes   {0}" -f $info.Length)
        Write-Host ("  sha256  {0}" -f (Get-FileHash $dll -Algorithm SHA256).Hash)
        $script:BuildOk = $true
    }
    elseif ($uatExit -eq 0 -and -not $dllExists) {
        Write-Host 'BUILD REPORTED SUCCESS BUT PRODUCED NO DLL - treat as failure.' -ForegroundColor Red
        $script:BuildOk = $false
    }
    else {
        Write-Host "BUILD FAILED (UAT exit $uatExit)" -ForegroundColor Red
        Write-Host 'Full log: %APPDATA%\Unreal Engine\AutomationTool\Logs' -ForegroundColor DarkGray
        $script:BuildOk = $false
    }
}
finally {
    # Always restore, including on failure or interrupt.
    foreach ($entry in $moved) {
        if (Test-Path $entry.Parked) {
            Move-Item -LiteralPath $entry.Parked -Destination $entry.Source -Force
            Write-Host "  restored $($entry.Name)" -ForegroundColor DarkGray
        }
    }

    $failed = @()
    foreach ($entry in $moved) {
        if (-not (Test-Path $entry.Source)) { $failed += $entry.Name }
    }
    if ($failed.Count -gt 0) {
        Write-Host ''
        Write-Host "CRITICAL: these plugins were NOT restored: $($failed -join ', ')" -ForegroundColor Red
        Write-Host "Recover them manually from: $parkingDir" -ForegroundColor Red
    }
    else {
        if (Test-Path $parkingDir) { Remove-Item $parkingDir -Recurse -Force -ErrorAction SilentlyContinue }
        if ($moved.Count -gt 0) { Write-Host 'All Houdini plugins restored.' -ForegroundColor Green }
    }

    if (-not $KeepOutput -and -not $script:BuildOk -and (Test-Path $OutputRoot)) {
        Remove-Item $OutputRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}

if (-not $script:BuildOk) { exit 1 }
exit 0
