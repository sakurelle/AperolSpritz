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

    $manualSource = Join-Path (Split-Path -Parent $PSScriptRoot) "docs\Руководство оператора.pdf"
    if (-not (Test-Path -LiteralPath $manualSource -PathType Leaf)) { throw "Missing operator manual PDF: $manualSource" }
    $manualInfo = Get-Item -LiteralPath $manualSource
    if ($manualInfo.Length -le 5) { throw "Operator manual PDF is empty: $manualSource" }
    $manualStream = [IO.File]::OpenRead($manualSource)
    try {
        $signatureBytes = New-Object byte[] 5
        if ($manualStream.Read($signatureBytes, 0, 5) -ne 5) { throw "Could not read operator manual PDF signature" }
        $signature = [Text.Encoding]::ASCII.GetString($signatureBytes)
        if ($signature -ne "%PDF-") { throw "Operator manual has an invalid PDF signature: $signature" }
    } finally {
        $manualStream.Dispose()
    }

    $sourcePath = (Resolve-Path -LiteralPath $source).Path
    $manualOutputDir = Join-Path $sourcePath "docs"
    if (Test-Path -LiteralPath $manualOutputDir) {
        $resolvedManualOutputDir = (Resolve-Path -LiteralPath $manualOutputDir).Path
        $expectedManualOutputDir = [IO.Path]::GetFullPath((Join-Path $sourcePath "docs"))
        if ($resolvedManualOutputDir -ne $expectedManualOutputDir) { throw "Refusing to replace unexpected docs path: $resolvedManualOutputDir" }
        Remove-Item -LiteralPath $resolvedManualOutputDir -Recurse -Force
    }
    New-Item -ItemType Directory -Path $manualOutputDir | Out-Null
    Copy-Item -LiteralPath $manualSource -Destination (Join-Path $manualOutputDir "Руководство оператора.pdf")

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
        $manualEntryPath = "AperolSpritz/docs/Руководство оператора.pdf"
        $manualEntry = $archive.Entries | Where-Object { $_.FullName.Replace('\', '/') -eq $manualEntryPath }
        if (@($manualEntry).Count -ne 1) { throw "Operator manual is missing or duplicated in ZIP" }
        if ($manualEntry.Length -le 5) { throw "Operator manual is empty in ZIP" }
        $pdfEntries = @($archive.Entries | Where-Object { $_.FullName.Replace('\', '/') -like "AperolSpritz/docs/*.pdf" })
        if ($pdfEntries.Count -ne 1) { throw "The docs directory must contain exactly one PDF" }
        $manualZipStream = $manualEntry.Open()
        try {
            $zipSignatureBytes = New-Object byte[] 5
            if ($manualZipStream.Read($zipSignatureBytes, 0, 5) -ne 5 -or [Text.Encoding]::ASCII.GetString($zipSignatureBytes) -ne "%PDF-") {
                throw "Operator manual has an invalid PDF signature in ZIP"
            }
        } finally {
            $manualZipStream.Dispose()
        }
        $forbidden = @($entries | Where-Object {
            $_ -match '(^|/)(\.venv|build|tests|release|\.git|\.pio)(/|$)' -or
            $_ -match '(^|/)(settings\.json|measurements\.db|secrets\.txt)$' -or
            $_ -match '\.(doc|docx|py|spec|db)$'
        })
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
