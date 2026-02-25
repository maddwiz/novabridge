param(
    [Parameter(Mandatory = $false)][string]$Project = "",
    [Parameter(Mandatory = $false)][int]$Port = 30010,
    [Parameter(Mandatory = $false)][switch]$SkipAssistant,
    [Parameter(Mandatory = $false)][switch]$NoBrowser
)

$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$envFile = if ([string]::IsNullOrWhiteSpace($env:NOVABRIDGE_ENV_FILE)) { Join-Path $Root 'novabridge.env' } else { $env:NOVABRIDGE_ENV_FILE }

function Import-EnvFile([string]$Path) {
    foreach ($line in Get-Content -Path $Path) {
        $trimmed = $line.Trim()
        if ([string]::IsNullOrWhiteSpace($trimmed) -or $trimmed.StartsWith('#')) {
            continue
        }
        $parts = $trimmed -split '=', 2
        if ($parts.Length -ne 2) {
            continue
        }
        $key = $parts[0].Trim()
        $value = $parts[1].Trim().Trim('"').Trim("'")
        if (-not [string]::IsNullOrWhiteSpace($key)) {
            [System.Environment]::SetEnvironmentVariable($key, $value, 'Process')
        }
    }
}

if (Test-Path $envFile) {
    Write-Host "[one-click] Loading env from $envFile"
    Import-EnvFile -Path $envFile
}

if ([string]::IsNullOrWhiteSpace($Project)) {
    if (-not [string]::IsNullOrWhiteSpace($env:NOVABRIDGE_PROJECT)) {
        $Project = $env:NOVABRIDGE_PROJECT
    } else {
        $Project = Join-Path $Root 'NovaBridgeDefault\NovaBridgeDefault.uproject'
    }
}

if ($Port -eq 30010 -and -not [string]::IsNullOrWhiteSpace($env:NOVABRIDGE_PORT)) {
    $Port = [int]$env:NOVABRIDGE_PORT
}

Write-Host "[one-click] Bootstrapping NovaBridge plugin + UE project..."
& (Join-Path $Root 'scripts\setup_win.ps1') -Project $Project -Port $Port

if (-not $SkipAssistant.IsPresent -and $env:NOVABRIDGE_SKIP_ASSISTANT -ne '1') {
    $node = Get-Command node -ErrorAction SilentlyContinue
    if ($null -eq $node) {
        Write-Warning "[one-click] Node.js not found; skipping assistant-server."
    } else {
        $existing = Get-CimInstance Win32_Process |
            Where-Object { $_.CommandLine -like '*assistant-server\\server.js*' } |
            Select-Object -First 1
        if ($null -ne $existing) {
            Write-Host "[one-click] assistant-server already running."
        } else {
            Write-Host "[one-click] Starting assistant-server..."
            $assistantScript = Join-Path $Root 'assistant-server\server.js'
            Start-Process -FilePath $node.Source -ArgumentList "`"$assistantScript`"" -WorkingDirectory $Root -WindowStyle Hidden
        }
    }
}

$assistantPort = if ([string]::IsNullOrWhiteSpace($env:NOVABRIDGE_ASSISTANT_PORT)) { 30016 } else { [int]$env:NOVABRIDGE_ASSISTANT_PORT }
if (-not $NoBrowser.IsPresent -and $env:NOVABRIDGE_OPEN_STUDIO -ne '0') {
    $url = "http://127.0.0.1:$assistantPort/nova/studio"
    Write-Host "[one-click] Opening Studio: $url"
    Start-Process $url | Out-Null
}

Write-Host "[one-click] Done."
