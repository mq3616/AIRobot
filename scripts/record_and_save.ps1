param(
    [string]$Port = "COM3",
    [ValidateSet("left", "right")]
    [string]$Channel = "left"
)

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$captureScript = Join-Path $PSScriptRoot "capture_wav_from_serial.ps1"

if (-not (Test-Path $captureScript)) {
    throw "Capture script not found: $captureScript"
}

$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$timestampedPath = Join-Path $projectRoot "recording_${Channel}_$timestamp.wav"
$latestPath = Join-Path $projectRoot "latest_recording_${Channel}.wav"

& powershell -ExecutionPolicy Bypass -File $captureScript `
    -Port $Port `
    -Channel $Channel `
    -OutputPath $timestampedPath

Copy-Item -Path $timestampedPath -Destination $latestPath -Force

Write-Host "Saved recording to:"
Write-Host "  $timestampedPath"
Write-Host "Updated latest file:"
Write-Host "  $latestPath"
