[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$Tag)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
Push-Location $PSScriptRoot
try {
    if ($Tag -notmatch '^v\d+\.\d+\.\d+$') { throw "Invalid tag: $Tag" }
    $version = (Get-Content -LiteralPath "VERSION" -Raw).Trim()
    if ($Tag -ne "v$version") { throw "Tag $Tag does not match desktop version v$version" }
    $source = Join-Path $PSScriptRoot "dist\AperolSpritz"
    foreach ($required in @("AperolSpritz.exe", "_internal", "README.txt")) {
        if (-not (Test-Path -LiteralPath (Join-Path $source $required))) { throw "Missing release input: $required" }
    }
    $releaseDir = Join-Path $PSScriptRoot "release"
    New-Item -ItemType Directory -Path $releaseDir -Force | Out-Null
    $zip = Join-Path $releaseDir "AperolSpritz-Windows-x64.zip"
    $checksum = "$zip.sha256"
    Remove-Item -LiteralPath $zip, $checksum -Force -ErrorAction SilentlyContinue
    Compress-Archive -LiteralPath $source -DestinationPath $zip -CompressionLevel Optimal

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::OpenRead($zip)
    try {
        $entries = @($archive.Entries | ForEach-Object { $_.FullName.Replace('\', '/') })
        if ("AperolSpritz/AperolSpritz.exe" -notin $entries) { throw "EXE is absent from ZIP" }
        if ("AperolSpritz/README.txt" -notin $entries) { throw "README.txt is absent from ZIP" }
        if (-not ($entries | Where-Object { $_ -like "AperolSpritz/_internal/*" })) { throw "_internal is absent from ZIP" }
        $forbidden = @($entries | Where-Object { $_ -match '(^|/)(\.venv|build|tests|release|\.git|\.pio)(/|$)' -or $_ -match '(^|/)(settings\.json|measurements\.db|secrets\.txt)$' })
        if ($forbidden) { throw "Forbidden ZIP entries: $($forbidden -join ', ')" }
    } finally {
        $archive.Dispose()
    }
    $hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
    Set-Content -LiteralPath $checksum -Value "$hash  AperolSpritz-Windows-x64.zip" -Encoding ascii
    Write-Output "Release archive: $zip"
    Write-Output "SHA256: $hash"
} finally {
    Pop-Location
}
