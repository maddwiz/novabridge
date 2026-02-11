param(
    [Parameter(Mandatory=$true)][string]$RepoUrl,
    [Parameter(Mandatory=$true)][string]$Token,
    [Parameter(Mandatory=$true)][string]$UERoot,
    [Parameter(Mandatory=$false)][string]$RunnerName = $env:COMPUTERNAME,
    [Parameter(Mandatory=$false)][string]$Labels = "self-hosted,Windows,X64,unreal"
)

$ErrorActionPreference = 'Stop'

$RunnerVersion = "2.327.1"
$RunnerDir = "$env:USERPROFILE\actions-runner-novabridge"
$ZipName = "actions-runner-win-x64-$RunnerVersion.zip"
$ZipPath = Join-Path $RunnerDir $ZipName
$DownloadUrl = "https://github.com/actions/runner/releases/download/v$RunnerVersion/$ZipName"

New-Item -ItemType Directory -Force -Path $RunnerDir | Out-Null
Set-Location $RunnerDir

if (-not (Test-Path $ZipPath)) {
    Invoke-WebRequest -Uri $DownloadUrl -OutFile $ZipPath
}

if (-not (Test-Path (Join-Path $RunnerDir 'run.cmd'))) {
    Expand-Archive -Path $ZipPath -DestinationPath $RunnerDir -Force
}

if (-not (Test-Path (Join-Path $RunnerDir '.runner'))) {
    .\config.cmd --unattended --url $RepoUrl --token $Token --name $RunnerName --labels $Labels --replace
} else {
    Write-Host "Runner already configured in $RunnerDir"
}

[System.Environment]::SetEnvironmentVariable("UE_ROOT", $UERoot, "Machine")
"UE_ROOT=$UERoot" | Set-Content -Path (Join-Path $RunnerDir '.env') -Encoding UTF8

.\svc.cmd install
.\svc.cmd start

Write-Host "Runner configured and started."
Write-Host "Runner dir: $RunnerDir"
Write-Host "UE_ROOT: $UERoot"
