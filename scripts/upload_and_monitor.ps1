param(
    [string]$Port = "COM3",
    [int]$Baud = 115200,
    [string]$Environment = "esp32-s3-devkit",
    [switch]$OpenMonitor
)

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$pio = Join-Path $env:USERPROFILE ".platformio\penv\Scripts\platformio.exe"

if (-not (Test-Path $pio)) {
    throw "PlatformIO not found: $pio"
}

function Get-PlatformIOMonitorProcesses {
    param([string]$TargetPort)

    Get-CimInstance Win32_Process |
        Where-Object {
            (
                $_.Name -match "^python(\.exe)?$|^platformio(\.exe)?$" -and
                $_.CommandLine -match "device monitor" -and
                $_.CommandLine -match [regex]::Escape($TargetPort)
            ) -or (
                $_.Name -match "^powershell(\.exe)?$" -and
                $_.CommandLine -match "device monitor" -and
                $_.CommandLine -match [regex]::Escape($TargetPort)
            )
        }
}

function Stop-PlatformIOMonitorProcesses {
    param([string]$TargetPort)

    $monitorProcesses = @(Get-PlatformIOMonitorProcesses -TargetPort $TargetPort)
    if ($monitorProcesses.Count -eq 0) {
        Write-Host "No PlatformIO monitor process found on $TargetPort"
        return
    }

    foreach ($process in $monitorProcesses) {
        Write-Host "Stopping PlatformIO monitor PID $($process.ProcessId) on $TargetPort"
        Stop-Process -Id $process.ProcessId -Force -ErrorAction SilentlyContinue
    }

    Start-Sleep -Milliseconds 500
}

function Get-MonitorShellProcesses {
    param([string]$TargetPort)

    Get-CimInstance Win32_Process |
        Where-Object {
            $_.Name -match "^powershell(\.exe)?$|^pwsh(\.exe)?$" -and
            $_.CommandLine -match "device monitor" -and
            $_.CommandLine -match [regex]::Escape($TargetPort)
        }
}

function Stop-MonitorShellProcesses {
    param([string]$TargetPort)

    $shellProcesses = @(Get-MonitorShellProcesses -TargetPort $TargetPort)
    foreach ($process in $shellProcesses) {
        Write-Host "Closing monitor shell PID $($process.ProcessId) on $TargetPort"
        Stop-Process -Id $process.ProcessId -Force -ErrorAction SilentlyContinue
    }

    if ($shellProcesses.Count -gt 0) {
        Start-Sleep -Milliseconds 500
    }
}

Stop-PlatformIOMonitorProcesses -TargetPort $Port
Stop-MonitorShellProcesses -TargetPort $Port

Write-Host "Uploading firmware to $Port..."
& $pio run -e $Environment -t upload --upload-port $Port
if ($LASTEXITCODE -ne 0) {
    throw "Upload failed"
}

if ($OpenMonitor) {
    Write-Host "Opening serial monitor on $Port at $Baud baud..."
    Start-Process -FilePath "powershell.exe" `
        -ArgumentList @(
            "-NoExit",
            "-Command",
            "& `"$pio`" device monitor -p $Port -b $Baud"
        ) `
        -WorkingDirectory $projectRoot
} else {
    Write-Host "Upload complete. Monitor window remains closed."
}
