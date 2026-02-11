param(
    [Parameter(Mandatory=$false)][string]$RepoRoot = (Get-Location).Path,
    [Parameter(Mandatory=$false)][string]$OutputRoot = "$PWD\artifacts-win",
    [Parameter(Mandatory=$false)][string]$UERootOverride = ""
)

$ErrorActionPreference = 'Stop'

$ueRoot = ""
if (-not [string]::IsNullOrWhiteSpace($UERootOverride)) {
    $ueRoot = $UERootOverride
} elseif (-not [string]::IsNullOrWhiteSpace($env:UE_ROOT)) {
    $ueRoot = $env:UE_ROOT
} else {
    throw "UE root not set. Pass -UERootOverride or set UE_ROOT on the runner."
}

$runUat = Join-Path $ueRoot 'Engine\Build\BatchFiles\RunUAT.bat'
$pluginFile = Join-Path $RepoRoot 'NovaBridge\NovaBridge.uplugin'
$packageDir = Join-Path $OutputRoot 'NovaBridge-Win64'
$zipPath = Join-Path $OutputRoot 'NovaBridge-Win64.zip'
$manifest = Join-Path $OutputRoot 'manifest.txt'

if (-not (Test-Path $runUat)) { throw "RunUAT not found: $runUat" }
if (-not (Test-Path $pluginFile)) { throw "Plugin descriptor not found: $pluginFile" }

if (Test-Path $packageDir) { Remove-Item -Recurse -Force $packageDir }
if (Test-Path $zipPath) { Remove-Item -Force $zipPath }
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

& $runUat BuildPlugin `
    -Plugin="$pluginFile" `
    -Package="$packageDir" `
    -TargetPlatforms=Win64 `
    -Rocket

Compress-Archive -Path "$packageDir\*" -DestinationPath $zipPath -Force

@(
  "platform=Win64",
  "ue_root=$ueRoot",
  "plugin=$pluginFile",
  "package_dir=$packageDir",
  "zip=$zipPath",
  "built_at_utc=$((Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ'))"
) | Set-Content -Path $manifest -Encoding UTF8

Write-Host "Built: $zipPath"
