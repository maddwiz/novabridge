param(
    [Parameter(Mandatory = $false)][string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [Parameter(Mandatory = $false)][string]$Version = "v0.9.0"
)

$ErrorActionPreference = "Stop"

$distDir = Join-Path $RepoRoot "dist"
$pkgName = "NovaBridge-$Version"
$pkgDir = Join-Path $distDir $pkgName
$zipPath = "$pkgDir.zip"

function Copy-Tree {
    param(
        [string]$Source,
        [string]$Destination
    )
    robocopy $Source $Destination /E `
        /XD .git node_modules __pycache__ Binaries Intermediate Saved DerivedDataCache "Content\Developers" "Content\Collections" `
        /XF *.pyc *.log `
        /NFL /NDL /NJH /NJS /NC /NS | Out-Null
}

if (Test-Path $pkgDir) { Remove-Item -Recurse -Force $pkgDir }
if (Test-Path $zipPath) { Remove-Item -Force $zipPath }
New-Item -ItemType Directory -Force -Path $pkgDir | Out-Null

Copy-Tree (Join-Path $RepoRoot "NovaBridge") $pkgDir
Copy-Tree (Join-Path $RepoRoot "NovaBridgeDemo") $pkgDir
Copy-Tree (Join-Path $RepoRoot "NovaBridgeDefault") $pkgDir
Copy-Tree (Join-Path $RepoRoot "python-sdk") $pkgDir
Copy-Tree (Join-Path $RepoRoot "mcp-server") $pkgDir
Copy-Tree (Join-Path $RepoRoot "blender") $pkgDir
Copy-Tree (Join-Path $RepoRoot "extensions") $pkgDir
Copy-Tree (Join-Path $RepoRoot "examples") $pkgDir
Copy-Tree (Join-Path $RepoRoot "scripts") $pkgDir
Copy-Tree (Join-Path $RepoRoot "demo") $pkgDir
Copy-Tree (Join-Path $RepoRoot "site") $pkgDir

$docsDest = Join-Path $pkgDir "docs"
New-Item -ItemType Directory -Force $docsDest | Out-Null
foreach ($doc in @("API.md", "SETUP_LINUX.md", "SETUP_MAC.md", "SETUP_WINDOWS.md", "SMOKE_TEST_CHECKLIST.md", "MACOS_SMOKE_TEST.md")) {
    $src = Join-Path $RepoRoot "docs\$doc"
    if (Test-Path $src) { Copy-Item -Force $src $docsDest }
}

Copy-Item -Force (Join-Path $RepoRoot "README.md") $pkgDir
Copy-Item -Force (Join-Path $RepoRoot "QUICK_START.md") $pkgDir
Copy-Item -Force (Join-Path $RepoRoot ".gitignore") (Join-Path $pkgDir ".gitignore.example")
foreach ($doc in @("INSTALL.md", "BuyerGuide.md", "CHANGELOG.md", "SUPPORT.md", "EULA.txt")) {
    $src = Join-Path $RepoRoot $doc
    if (Test-Path $src) { Copy-Item -Force $src $pkgDir }
}
if (Test-Path (Join-Path $RepoRoot "LICENSE")) {
    Copy-Item -Force (Join-Path $RepoRoot "LICENSE") $pkgDir
}

Compress-Archive -Path (Join-Path $pkgDir "*") -DestinationPath $zipPath -Force
Write-Host "Created package: $zipPath"
