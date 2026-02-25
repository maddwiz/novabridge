param(
    [Parameter(Mandatory = $false)][string]$Project = "$PSScriptRoot\..\NovaBridgeDefault\NovaBridgeDefault.uproject",
    [Parameter(Mandatory = $false)][int]$Port = 30010,
    [Parameter(Mandatory = $false)][switch]$NoLaunch
)

$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

function Resolve-UProject([string]$InputPath) {
    if (Test-Path $InputPath -PathType Leaf -and $InputPath.ToLower().EndsWith('.uproject')) {
        return (Resolve-Path $InputPath).Path
    }
    if (Test-Path $InputPath -PathType Container) {
        $first = Get-ChildItem -Path $InputPath -Filter *.uproject | Select-Object -First 1
        if ($null -ne $first) {
            return $first.FullName
        }
    }
    return $null
}

$uproject = Resolve-UProject $Project
if ($null -eq $uproject) {
    throw "Could not resolve .uproject from: $Project"
}

$projectDir = Split-Path $uproject -Parent
$pluginSrc = Join-Path $Root 'NovaBridge'
$pluginDst = Join-Path $projectDir 'Plugins\NovaBridge'

New-Item -ItemType Directory -Force -Path (Join-Path $projectDir 'Plugins') | Out-Null
if (Test-Path $pluginDst) { Remove-Item -Recurse -Force $pluginDst }
Copy-Item -Recurse -Force $pluginSrc $pluginDst

Write-Host "[setup] Copied plugin to $pluginDst"
Write-Host "[setup] Suggested launch command:"
Write-Host "UnrealEditor.exe `"$uproject`" -RenderOffScreen -nosplash -unattended -nopause -NovaBridgePort=$Port"

if ($NoLaunch) {
    Write-Host "[setup] Skipping auto-launch (--NoLaunch)."
    exit 0
}

$editor = $env:UE_EDITOR_BIN
if ([string]::IsNullOrWhiteSpace($editor)) {
    $candidates = @(
        'C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe',
        'C:\Program Files\Epic Games\UE_5.6\Engine\Binaries\Win64\UnrealEditor.exe'
    )
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            $editor = $candidate
            break
        }
    }
}

if ([string]::IsNullOrWhiteSpace($editor) -or -not (Test-Path $editor)) {
    Write-Warning "UnrealEditor.exe not found. Plugin copied and project is ready."
    exit 0
}

Start-Process -FilePath $editor -ArgumentList "`"$uproject`" -RenderOffScreen -nosplash -unattended -nopause -NovaBridgePort=$Port"
Write-Host "[setup] UnrealEditor launched in background."
