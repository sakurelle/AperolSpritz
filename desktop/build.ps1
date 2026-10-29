[CmdletBinding()]
param(
    [string]$PythonExecutable = "",
    [switch]$SkipInstall,
    [switch]$SkipTests,
    [switch]$UsePythonEnvironment
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
Push-Location $PSScriptRoot
try {
    function Test-PythonRuntime([string]$Candidate) {
        if (-not $Candidate -or -not (Test-Path -LiteralPath $Candidate -PathType Leaf)) { return $false }
        & $Candidate -c "import sys, tkinter; assert sys.version_info >= (3, 9), sys.version; tkinter.Tcl()" *> $null
        return $LASTEXITCODE -eq 0
    }

    if (-not $PythonExecutable) {
        $candidates = @()
        $launcher = Get-Command py -ErrorAction SilentlyContinue
        if ($launcher) {
            $candidate = & $launcher.Source -3.12 -c "import sys; print(sys.executable)" 2>$null
            if ($LASTEXITCODE -eq 0 -and $candidate) { $candidates += $candidate.Trim() }
        }
        $python = Get-Command python -ErrorAction SilentlyContinue
        if ($python) { $candidates += $python.Source }
        foreach ($candidate in ($candidates | Select-Object -Unique)) {
            if (Test-PythonRuntime $candidate) { $PythonExecutable = $candidate; break }
        }
        if (-not $PythonExecutable) { throw "Python 3.9 or newer with a working tkinter/Tcl installation is required" }
    }
    if (-not (Test-PythonRuntime $PythonExecutable)) { throw "Python 3.9 or newer with a working tkinter/Tcl installation is required" }

    if ($UsePythonEnvironment) {
        $venvPython = $PythonExecutable
    } else {
        $venvPython = Join-Path $PSScriptRoot ".venv\Scripts\python.exe"
        if (-not (Test-Path -LiteralPath $venvPython -PathType Leaf)) {
            & $PythonExecutable -m venv (Join-Path $PSScriptRoot ".venv")
            if ($LASTEXITCODE -ne 0) { throw "Failed to create the virtual environment" }
        }
    }
    if (-not $SkipInstall) {
        & $venvPython -m pip install --upgrade pip
        if ($LASTEXITCODE -ne 0) { throw "Failed to upgrade pip" }
        & $venvPython -m pip install -r requirements.txt -r requirements-build.txt
        if ($LASTEXITCODE -ne 0) { throw "Failed to install build dependencies" }
    }
    & $venvPython -c "import tkinter, requests, openpyxl, PyInstaller; tkinter.Tcl()"
    if ($LASTEXITCODE -ne 0) { throw "Required Python modules are unavailable" }
    & $venvPython -m compileall -q app.py aperolspritz tests
    if ($LASTEXITCODE -ne 0) { throw "Python syntax check failed" }
    if (-not $SkipTests) {
        & $venvPython -m pytest -q
        if ($LASTEXITCODE -ne 0) { throw "Unit tests failed" }
    }
    & $venvPython -m PyInstaller --noconfirm --clean AperolSpritz.spec
    if ($LASTEXITCODE -ne 0) { throw "PyInstaller failed" }

    $outputDir = Join-Path $PSScriptRoot "dist\AperolSpritz"
    $executable = Join-Path $outputDir "AperolSpritz.exe"
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) { throw "Missing $executable" }
    if (-not (Test-Path -LiteralPath (Join-Path $outputDir "_internal") -PathType Container)) { throw "Missing _internal directory" }
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot "README.release.txt") -Destination (Join-Path $outputDir "README.txt") -Force
    Write-Output "Windows onedir build: $executable"
} finally {
    Pop-Location
}
