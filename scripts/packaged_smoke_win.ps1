param(
    [Parameter(Mandatory = $true)][string]$RepoRoot,
    [Parameter(Mandatory = $true)][string]$ArtifactsRoot,
    [Parameter(Mandatory = $false)][string]$Version = "v0.9.0"
)

$ErrorActionPreference = "Stop"

$distZip = Join-Path $RepoRoot "dist\NovaBridge-$Version.zip"
if (-not (Test-Path $distZip)) {
    throw "Package zip not found: $distZip"
}

$ts = Get-Date -Format "yyyyMMdd-HHmmss"
$packDir = Join-Path $ArtifactsRoot "packaged-validation-$ts"
$unz = Join-Path $packDir "unzipped"
$proj = Join-Path $packDir "WinSmokePackaged"

New-Item -ItemType Directory -Force $packDir | Out-Null
New-Item -ItemType Directory -Force $unz | Out-Null
New-Item -ItemType Directory -Force $proj | Out-Null

robocopy (Join-Path $RepoRoot "NovaBridgeDefault") $proj /E /XD Binaries Intermediate Saved | Out-Null
& "C:\Program Files\7-Zip\7z.exe" x $distZip -o"$unz" | Out-Null

$pkgRoot = Join-Path $unz "NovaBridge-$Version"
if (-not (Test-Path $pkgRoot)) {
    $pkgRoot = $unz
}
$pluginDst = Join-Path $proj "Plugins\NovaBridge"
New-Item -ItemType Directory -Force (Join-Path $proj "Plugins") | Out-Null
New-Item -ItemType Directory -Force $pluginDst | Out-Null

$pluginSrc = Join-Path $pkgRoot "NovaBridge"
if (-not (Test-Path $pluginSrc)) {
    $pluginSrc = Join-Path $pkgRoot "Plugins\\NovaBridge"
}

$rootUplugin = Join-Path $pkgRoot "NovaBridge.uplugin"
$rootSource = Join-Path $pkgRoot "Source"
if ((Test-Path $rootUplugin) -and (Test-Path $rootSource)) {
    Copy-Item -Force $rootUplugin $pluginDst
    $dstSource = Join-Path $pluginDst "Source"
    New-Item -ItemType Directory -Force $dstSource | Out-Null
    Copy-Item -Recurse -Force $rootSource $dstSource
} else {
    Copy-Item -Recurse -Force $pluginSrc $pluginDst
}

Write-Host "Packaged project: $proj"
