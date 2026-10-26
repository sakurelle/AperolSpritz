[CmdletBinding()]
param(
    [string]$Executable = "",
    [int]$TimeoutSeconds = 15
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
if (-not $Executable) { $Executable = Join-Path $PSScriptRoot "..\dist\AperolSpritz\AperolSpritz.exe" }
$Executable = (Resolve-Path -LiteralPath $Executable).Path
$smokeRoot = Join-Path $env:USERPROFILE ("AS-smoke-" + [guid]::NewGuid().ToString("N").Substring(0, 8))
$stagedApp = Join-Path $smokeRoot "AperolSpritz"
$smokeAppData = Join-Path $smokeRoot "AppData"
New-Item -ItemType Directory -Path $smokeRoot | Out-Null
Copy-Item -LiteralPath (Split-Path -Parent $Executable) -Destination $stagedApp -Recurse
New-Item -ItemType Directory -Path $smokeAppData | Out-Null
$previousAppData = $env:APPDATA
$previousSmoke = $env:APEROLSPRITZ_SMOKE_TEST
try {
    $env:APPDATA = $smokeAppData
    $env:APEROLSPRITZ_SMOKE_TEST = "1"
    $process = Start-Process -FilePath (Join-Path $stagedApp "AperolSpritz.exe") -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        throw "Frozen application did not finish its smoke test within $TimeoutSeconds seconds"
    }
    if ($process.ExitCode -ne 0) { throw "Frozen application exited with code $($process.ExitCode)" }
    $dataDir = Join-Path $smokeAppData "AperolSpritz"
    foreach ($name in @("smoke-ready.txt", "settings.json", "measurements.db")) {
        if (-not (Test-Path -LiteralPath (Join-Path $dataDir $name) -PathType Leaf)) { throw "Smoke output is missing: $name" }
    }
    Write-Output "Frozen smoke test passed with temporary APPDATA: $smokeRoot"
} finally {
    $env:APPDATA = $previousAppData
    $env:APEROLSPRITZ_SMOKE_TEST = $previousSmoke
    if (Test-Path -LiteralPath $smokeRoot) { Remove-Item -LiteralPath $smokeRoot -Recurse -Force }
}
