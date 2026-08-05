[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Project,

    [string]$EngineRoot = "C:/Program Files/Epic Games/UE_5.7",

    [string]$Cases = (Join-Path $PSScriptRoot "../Tests/Golden/cases.trans.local.json"),

    [string]$Python = "python",

    [ValidateRange(1, 10)]
    [int]$Repeat = 1,

    [string]$Case = "all",

    [switch]$UpdateBaseline,

    [switch]$Force,

    [ValidateRange(30, 3600)]
    [int]$TimeoutSeconds = 600
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Resolve-ExistingPath {
    param([string]$Path, [string]$Label)
    $resolved = Resolve-Path -LiteralPath $Path -ErrorAction SilentlyContinue
    if (-not $resolved) {
        throw "$Label does not exist: $Path"
    }
    return $resolved.Path
}

function Get-ExecutableVersion {
    param([string]$Path)
    $versionInfo = (Get-Item -LiteralPath $Path).VersionInfo
    foreach ($candidate in @($versionInfo.ProductVersion, $versionInfo.FileVersion)) {
        if (-not [string]::IsNullOrWhiteSpace($candidate)) {
            return $candidate.Trim()
        }
    }
    throw "Cannot determine executable version: $Path"
}

function Get-GitProvenance {
    param([string]$RepositoryRoot)
    $headOutput = @(& git -C $RepositoryRoot rev-parse HEAD 2>$null)
    if ($LASTEXITCODE -ne 0 -or $headOutput.Count -ne 1 -or $headOutput[0] -notmatch '^[0-9a-fA-F]{40}$') {
        throw "Cannot determine Git HEAD for: $RepositoryRoot"
    }
    $statusOutput = @(& git -C $RepositoryRoot status --porcelain --untracked-files=normal 2>$null)
    if ($LASTEXITCODE -ne 0) {
        throw "Cannot determine Git dirty state for: $RepositoryRoot"
    }
    return [ordered]@{
        repositoryPath = $RepositoryRoot
        head = [string]$headOutput[0]
        dirty = ($statusOutput.Count -gt 0)
    }
}

function Get-LoadedModulePath {
    param([int]$ProcessId, [string]$ModuleFileName)
    try {
        $runningProcess = Get-Process -Id $ProcessId -ErrorAction Stop
        foreach ($module in @($runningProcess.Modules)) {
            if ([string]::Equals([System.IO.Path]::GetFileName($module.FileName), $ModuleFileName, [System.StringComparison]::OrdinalIgnoreCase)) {
                return [System.IO.Path]::GetFullPath($module.FileName)
            }
        }
    }
    catch {
    }
    return $null
}

function Get-PluginDescriptorPath {
    param([string]$ModulePath)
    $directory = (Get-Item -LiteralPath $ModulePath).Directory
    while ($null -ne $directory) {
        $candidate = Join-Path $directory.FullName "ReadAllandExplains.uplugin"
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return [System.IO.Path]::GetFullPath($candidate)
        }
        $directory = $directory.Parent
    }
    return $null
}

function Get-CaseArray {
    param([object]$CaseConfig, [string]$Name)
    $property = $CaseConfig.PSObject.Properties[$Name]
    if ($null -eq $property -or $null -eq $property.Value) {
        return @()
    }
    return @($property.Value)
}

function Get-CompletePacks {
    param([string]$PacksRoot)
    $result = @{}
    if (-not (Test-Path -LiteralPath $PacksRoot -PathType Container)) {
        return $result
    }
    Get-ChildItem -LiteralPath $PacksRoot -Directory | ForEach-Object {
        if ($_.Name.EndsWith(".tmp", [System.StringComparison]::OrdinalIgnoreCase)) {
            return
        }
        $manifestPath = Join-Path $_.FullName "context-pack.json"
        if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
            return
        }
        try {
            $manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
            if ($manifest.state -eq "complete" -and $manifest.packId -eq $_.Name) {
                $result[$_.Name] = $_.FullName
            }
        }
        catch {
        }
    }
    return $result
}

function Select-NewPack {
    param(
        [hashtable]$Before,
        [hashtable]$After,
        [string[]]$ExpectedRoots
    )
    $matches = @()
    foreach ($entry in $After.GetEnumerator()) {
        if ($Before.ContainsKey($entry.Key)) {
            continue
        }
        $manifestPath = Join-Path $entry.Value "context-pack.json"
        $manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
        $roots = @($manifest.rootAssets)
        $missing = @($ExpectedRoots | Where-Object { $_ -notin $roots })
        if ($missing.Count -eq 0) {
            $matches += Get-Item -LiteralPath $entry.Value
        }
    }
    $selected = $matches | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
    if (-not $selected) {
        throw "No new complete Context Pack matched roots: $($ExpectedRoots -join ', ')"
    }
    return $selected.FullName
}

$projectPath = Resolve-ExistingPath $Project "Unreal project"
$casesPath = Resolve-ExistingPath $Cases "Golden cases config"
$editorCmd = Resolve-ExistingPath (Join-Path $EngineRoot "Engine/Binaries/Win64/UnrealEditor-Cmd.exe") "UnrealEditor-Cmd"
$goldenTool = Resolve-ExistingPath (Join-Path $PSScriptRoot "../Tests/Golden/golden_regression.py") "Golden regression tool"
$repositoryRoot = Resolve-ExistingPath (Join-Path $PSScriptRoot "..") "ReadAllandExplains repository"
$repositoryPluginDescriptor = Resolve-ExistingPath (Join-Path $repositoryRoot "ReadAllandExplains.uplugin") "ReadAllandExplains plugin descriptor"
$staticProvenance = [ordered]@{
    collectedUtc = [DateTime]::UtcNow.ToString("o")
    project = [ordered]@{
        path = $projectPath
    }
    engine = [ordered]@{
        commandPath = $editorCmd
        version = Get-ExecutableVersion $editorCmd
    }
    git = Get-GitProvenance $repositoryRoot
    pluginDescriptor = [ordered]@{
        path = $repositoryPluginDescriptor
        sha256 = (Get-FileHash -LiteralPath $repositoryPluginDescriptor -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}

$runningEditors = @(Get-Process UnrealEditor,UnrealEditor-Cmd -ErrorAction SilentlyContinue)
if ($runningEditors.Count -gt 0) {
    throw "An Unreal Editor or UnrealEditor-Cmd process is already running. Close it before Golden regression so exports can be associated with this run."
}

$config = Get-Content -LiteralPath $casesPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($config.schemaVersion -ne 1 -or -not $config.cases) {
    throw "Cases config must contain schemaVersion 1 and a cases array."
}
if ($UpdateBaseline -and $Repeat -gt 1) {
    throw "UpdateBaseline requires Repeat=1. Approve one export first, then use Repeat without UpdateBaseline to verify determinism."
}
$casesDirectory = Split-Path -Parent $casesPath

$selectedCases = @($config.cases | Where-Object { $Case -eq "all" -or $_.name -eq $Case })
if ($selectedCases.Count -eq 0) {
    throw "No Golden case matched '$Case'."
}
$seenCaseNames = @{}
foreach ($caseConfig in $selectedCases) {
    $caseName = [string]$caseConfig.name
    if ($caseName -notmatch '^[A-Za-z0-9._-]+$') {
        throw "Unsafe Golden case name: $caseName"
    }
    if ($seenCaseNames.ContainsKey($caseName)) {
        throw "Duplicate Golden case name: $caseName"
    }
    $seenCaseNames[$caseName] = $true
}

$projectRoot = Split-Path -Parent $projectPath
$packsRoot = Join-Path $projectRoot "Saved/ReadAllandExplainsExports/ContextPacks"
$managedBaselineRoot = [System.IO.Path]::GetFullPath((Join-Path $casesDirectory ".baselines"))
$reportsRoot = Join-Path $casesDirectory ".reports"
New-Item -ItemType Directory -Path $reportsRoot -Force | Out-Null
$overallExit = 0

foreach ($caseConfig in $selectedCases) {
    if (-not $caseConfig.name -or -not $caseConfig.rootAssets) {
        throw "Every case requires name and rootAssets."
    }
    $rootAssets = @($caseConfig.rootAssets)
    foreach ($rootAsset in $rootAssets) {
        if ($rootAsset -notmatch '^/Game/[A-Za-z0-9_./-]+\.[A-Za-z0-9_-]+$') {
            throw "Unsafe or invalid root asset path in $($caseConfig.name): $rootAsset"
        }
    }

    for ($run = 1; $run -le $Repeat; $run++) {
        Write-Host "[Golden] Exporting $($caseConfig.name), run $run/$Repeat"
        $before = Get-CompletePacks $packsRoot
        $execCommand = "ReadAllandExplains.ExportContextPack $($rootAssets -join ' ')"
        $arguments = @(
            "`"$projectPath`"",
            "-ExecCmds=`"$execCommand`"",
            "-unattended",
            "-nop4",
            "-nullrhi",
            "-nosplash",
            "-nosound",
            "-UTF8Output"
        )
        $process = Start-Process -FilePath $editorCmd -ArgumentList $arguments -PassThru
        $actualPack = $null
        $loadedModulePath = $null
        $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
        try {
            while ([DateTime]::UtcNow -lt $deadline) {
                if (-not $loadedModulePath) {
                    $loadedModulePath = Get-LoadedModulePath $process.Id "UnrealEditor-ReadAllandExplains.dll"
                }
                $after = Get-CompletePacks $packsRoot
                try {
                    $actualPack = Select-NewPack $before $after $rootAssets
                    break
                }
                catch {
                }
                $process.Refresh()
                if ($process.HasExited) {
                    throw "UnrealEditor-Cmd exited before publishing a matching Context Pack. Exit code: $($process.ExitCode)."
                }
                Start-Sleep -Milliseconds 250
            }
            if (-not $actualPack) {
                throw "Timed out after $TimeoutSeconds seconds waiting for a complete Context Pack for $($caseConfig.name)."
            }
        }
        finally {
            $process.Refresh()
            if (-not $process.HasExited) {
                Stop-Process -Id $process.Id -Force
                $process.WaitForExit()
            }
        }

        $baselinePath = [string]$caseConfig.baseline
        if ([System.IO.Path]::IsPathRooted($baselinePath)) {
            throw "Golden baseline must be relative to the cases config: $baselinePath"
        }
        $baselinePath = [System.IO.Path]::GetFullPath((Join-Path $casesDirectory $baselinePath))
        $baselineRootWithSeparator = $managedBaselineRoot.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
        if (-not $baselinePath.StartsWith($baselineRootWithSeparator, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Golden baseline must be inside ${managedBaselineRoot}: $baselinePath"
        }
        $runtimeCase = [ordered]@{
            name = "$($caseConfig.name)-run-$run"
            actualPack = $actualPack
            baseline = $baselinePath
            requiredAssetPaths = @(Get-CaseArray $caseConfig "requiredAssetPaths")
            requiredMarkers = @(Get-CaseArray $caseConfig "requiredMarkers")
            forbiddenMarkers = @(Get-CaseArray $caseConfig "forbiddenMarkers")
        }
        $runProvenance = [ordered]@{
            collectedUtc = $staticProvenance.collectedUtc
            project = $staticProvenance.project
            engine = $staticProvenance.engine
            git = $staticProvenance.git
            pluginDescriptor = [ordered]@{
                path = $staticProvenance.pluginDescriptor.path
                sha256 = $staticProvenance.pluginDescriptor.sha256
                source = "repository"
            }
        }
        if ($loadedModulePath -and (Test-Path -LiteralPath $loadedModulePath -PathType Leaf)) {
            $runProvenance["pluginModule"] = [ordered]@{
                path = $loadedModulePath
                sha256 = (Get-FileHash -LiteralPath $loadedModulePath -Algorithm SHA256).Hash.ToLowerInvariant()
            }
            $loadedPluginDescriptor = Get-PluginDescriptorPath $loadedModulePath
            if ($loadedPluginDescriptor) {
                $runProvenance["pluginDescriptor"] = [ordered]@{
                    path = $loadedPluginDescriptor
                    sha256 = (Get-FileHash -LiteralPath $loadedPluginDescriptor -Algorithm SHA256).Hash.ToLowerInvariant()
                    source = "loadedModule"
                }
            }
        }
        $runtimeConfig = [ordered]@{
            schemaVersion = 1
            provenance = $runProvenance
            cases = @($runtimeCase)
        }
        $runtimeConfigPath = Join-Path $reportsRoot "$($caseConfig.name)-run-$run.config.json"
        $jsonReportPath = Join-Path $reportsRoot "$($caseConfig.name)-run-$run.json"
        $humanReportPath = Join-Path $reportsRoot "$($caseConfig.name)-run-$run.txt"
        $runtimeConfig | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $runtimeConfigPath -Encoding UTF8

        $toolArguments = @(
            $goldenTool,
            "cases",
            $runtimeConfigPath,
            "--format",
            "human",
            "--json-report",
            $jsonReportPath,
            "--human-report",
            $humanReportPath
        )
        if ($UpdateBaseline) {
            $toolArguments += "--update-baselines"
        }
        if ($Force) {
            $toolArguments += "--force"
        }
        & $Python @toolArguments
        $caseExit = $LASTEXITCODE
        if ($caseExit -gt $overallExit) {
            $overallExit = $caseExit
        }
    }
}

exit $overallExit
