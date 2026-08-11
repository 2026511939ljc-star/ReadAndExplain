# Verifies that a freshly exported Context Pack really contains the CP7 facts.
#
# Until now CP7 and the Niagara HLSL work were only "code complete": the plugin
# compiled and the parity harness agreed, but nothing had been exported by the new
# binary. This script checks the two claims that can only be proven by a real
# export.
#
#   1. graphIndex is present, marked source=native, and its counts match the real
#      graphs arrays. A native index that disagrees with the graphs it describes is
#      worse than no index, so a mismatch is reported as a failure.
#   2. Niagara Custom HLSL nodes carry a non-empty sourceCode body, not just node
#      identity. Before this change the code body was absent entirely.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File Scripts\VerifyNativePack.ps1
#   powershell -ExecutionPolicy Bypass -File Scripts\VerifyNativePack.ps1 -PackPath <dir>

[CmdletBinding()]
param(
    [string] $ExportRoot = 'D:\UE5\Trans\Saved\ReadAllandExplainsExports\ContextPacks',
    [string] $PackPath
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($PackPath)) {
    # Default to the newest pack, which is what a verification run just produced.
    $latest = Get-ChildItem $ExportRoot -Directory -ErrorAction Stop |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $latest) { throw "No Context Pack found under $ExportRoot" }
    $PackPath = $latest.FullName
}

Write-Host "Pack: $PackPath" -ForegroundColor Cyan
Write-Host ("Written: {0}" -f (Get-Item $PackPath).LastWriteTime)
Write-Host ''

$metas = @(Get-ChildItem $PackPath -Recurse -Filter '*.meta.json' -ErrorAction SilentlyContinue)
if ($metas.Count -eq 0) { throw "No .meta.json files inside $PackPath" }

$withGraphs = 0
$nativeIndex = 0
$derivedOnly = 0
$indexMismatch = 0
$hlslNodes = 0
$hlslWithCode = 0
$hlslEmpty = 0
$hlslSamples = @()
$mismatchSamples = @()

foreach ($meta in $metas) {
    try {
        $json = Get-Content $meta.FullName -Raw -Encoding UTF8 | ConvertFrom-Json
    }
    catch { continue }

    $graphs = @($json.graphs)
    if ($graphs.Count -eq 0) { continue }
    $withGraphs++

    $index = $json.graphIndex
    if ($index -and $index.source -eq 'native') {
        $nativeIndex++

        # Recompute from the graphs array: the index must describe reality.
        $locators = @($index.graphs)
        if ($locators.Count -ne $graphs.Count) {
            $indexMismatch++
            $mismatchSamples += "$($meta.Name): graph count $($locators.Count) vs $($graphs.Count)"
        }
        else {
            for ($i = 0; $i -lt $graphs.Count; $i++) {
                $nodes = @($graphs[$i].nodes)
                $realNodes = $nodes.Count
                $realPins = 0
                foreach ($n in $nodes) { $realPins += @($n.pins).Count }
                $realLinks = @($graphs[$i].links).Count
                $loc = $locators[$i]
                if ($loc.nodeCount -ne $realNodes -or $loc.pinCount -ne $realPins -or $loc.linkCount -ne $realLinks) {
                    $indexMismatch++
                    $mismatchSamples += "$($meta.Name) graph $i : index ($($loc.nodeCount),$($loc.pinCount),$($loc.linkCount)) vs real ($realNodes,$realPins,$realLinks)"
                }
            }
        }
    }
    else {
        $derivedOnly++
    }

    foreach ($graph in $graphs) {
        foreach ($node in @($graph.nodes)) {
            if ($node.className -notlike '*CustomHlsl*') { continue }
            $hlslNodes++
            $code = $node.sourceCode
            if ([string]::IsNullOrWhiteSpace($code)) {
                $hlslEmpty++
            }
            else {
                $hlslWithCode++
                if ($hlslSamples.Count -lt 3) {
                    $firstLine = ($code -split "`n" | Where-Object { $_.Trim() } | Select-Object -First 1)
                    $hlslSamples += "$($meta.Name) :: $($node.title) :: $($code.Length) chars :: $($firstLine.Trim())"
                }
            }
        }
    }
}

Write-Host '--- Native graph index ---' -ForegroundColor Cyan
Write-Host ("assets with graphs        : {0}" -f $withGraphs)
Write-Host ("carrying source=native    : {0}" -f $nativeIndex)
Write-Host ("without native index      : {0}" -f $derivedOnly)
Write-Host ("index/graph mismatches    : {0}" -f $indexMismatch)
foreach ($s in $mismatchSamples | Select-Object -First 5) { Write-Host "    $s" -ForegroundColor Yellow }

Write-Host ''
Write-Host '--- Niagara Custom HLSL ---' -ForegroundColor Cyan
Write-Host ("CustomHlsl nodes found    : {0}" -f $hlslNodes)
Write-Host ("carrying sourceCode       : {0}" -f $hlslWithCode)
Write-Host ("empty sourceCode          : {0}" -f $hlslEmpty)
foreach ($s in $hlslSamples) { Write-Host "    $s" -ForegroundColor DarkGray }

Write-Host ''
$failures = @()
if ($nativeIndex -eq 0) { $failures += 'no asset carries a native graphIndex (was the pack exported by the CP7 build?)' }
if ($indexMismatch -gt 0) { $failures += "$indexMismatch native index entries disagree with the graphs they describe" }
if ($hlslNodes -gt 0 -and $hlslWithCode -eq 0) { $failures += 'CustomHlsl nodes exist but none carry sourceCode' }

if ($failures.Count -gt 0) {
    Write-Host 'RESULT: FAIL' -ForegroundColor Red
    foreach ($f in $failures) { Write-Host "  - $f" -ForegroundColor Red }
    exit 1
}

if ($hlslNodes -eq 0) {
    Write-Host 'RESULT: PASS (native index verified; no CustomHlsl node in this pack to check)' -ForegroundColor Yellow
    exit 0
}

Write-Host 'RESULT: PASS - native index and Niagara HLSL bodies both verified' -ForegroundColor Green
exit 0
