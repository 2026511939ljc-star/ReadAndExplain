param(
    [Parameter(Mandatory = $true)]
    [string]$WorkspaceRoot
)

$ErrorActionPreference = "Stop"

$pluginRoot = Split-Path -Parent $PSScriptRoot
$source = Join-Path $pluginRoot "skills/readallandexplains/SKILL.md"
$targetDirectory = Join-Path $WorkspaceRoot ".agent/skills/readallandexplains"
$target = Join-Path $targetDirectory "SKILL.md"

if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
    throw "Skill source not found: $source"
}

New-Item -ItemType Directory -Path $targetDirectory -Force | Out-Null
Copy-Item -LiteralPath $source -Destination $target -Force

$sourceHash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
$targetHash = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash

if ($sourceHash -ne $targetHash) {
    throw "Skill sync verification failed. Source=$sourceHash Target=$targetHash"
}

Write-Output "Skill synchronized: $target"
Write-Output "SHA256: $targetHash"
Write-Output "Reload CodeBuddy or start a new conversation to ensure the updated Skill is loaded."
