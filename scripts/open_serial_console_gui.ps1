param()

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$python = "python"
$scriptPath = Join-Path $PSScriptRoot "serial_console_gui.py"

if (-not (Test-Path $scriptPath)) {
    throw "GUI script not found: $scriptPath"
}

Set-Location $projectRoot
& $python $scriptPath
