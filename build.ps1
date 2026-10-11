$ErrorActionPreference = "Stop"

$env:UFBT_HOME = Join-Path $PSScriptRoot ".ufbt"
$ufbt = Join-Path $PSScriptRoot ".venv\Scripts\ufbt.exe"

if(-not (Test-Path $ufbt)) {
    Write-Error "uFBT is not installed. Run: .\.venv\Scripts\python.exe -m pip install --upgrade ufbt"
    exit 1
}

& $ufbt @args
exit $LASTEXITCODE
