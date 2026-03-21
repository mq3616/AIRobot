param(
    [string]$Port = "COM3",
    [int]$Baud = 115200,
    [string]$InputPath = "D:\Projects\AIRobot\temp_test_asr.wav",
    [int]$ResponseTimeoutSeconds = 30
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $InputPath)) {
    throw "Input WAV not found: $InputPath"
}

$bytes = [System.IO.File]::ReadAllBytes((Resolve-Path $InputPath))
$hexCharsPerLine = 64
$hex = [BitConverter]::ToString($bytes).Replace("-", "")

function Get-PlatformIOMonitorProcesses {
    param([string]$TargetPort)

    Get-CimInstance Win32_Process |
        Where-Object {
            (
                $_.Name -match "^python(\.exe)?$|^platformio(\.exe)?$" -and
                $_.CommandLine -match "device monitor" -and
                $_.CommandLine -match [regex]::Escape($TargetPort)
            ) -or (
                $_.Name -match "^powershell(\.exe)?$|^pwsh(\.exe)?$" -and
                $_.CommandLine -match "device monitor" -and
                $_.CommandLine -match [regex]::Escape($TargetPort)
            )
        }
}

function Stop-PlatformIOMonitorProcesses {
    param([string]$TargetPort)

    $monitorProcesses = @(Get-PlatformIOMonitorProcesses -TargetPort $TargetPort)
    foreach ($process in $monitorProcesses) {
        Write-Host "Stopping monitor PID $($process.ProcessId) on $TargetPort"
        Stop-Process -Id $process.ProcessId -Force -ErrorAction SilentlyContinue
    }

    if ($monitorProcesses.Count -gt 0) {
        Start-Sleep -Milliseconds 500
    }
}

function Read-BoardLine {
    param([System.IO.Ports.SerialPort]$SerialPort)

    try {
        return $SerialPort.ReadLine().Trim()
    }
    catch [System.TimeoutException] {
        return $null
    }
}

Stop-PlatformIOMonitorProcesses -TargetPort $Port

$serialPort = New-Object System.IO.Ports.SerialPort $Port, $Baud, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
$serialPort.NewLine = "`n"
$serialPort.ReadTimeout = 500
$serialPort.WriteTimeout = 10000

try {
    $serialPort.Open()
    Start-Sleep -Milliseconds 1200
    $serialPort.DiscardInBuffer()
    $serialPort.DiscardOutBuffer()

    Write-Host "Sending temp WAV to $Port from $InputPath"
    $serialPort.WriteLine("playhex $($bytes.Length)")

    $prepareDeadline = (Get-Date).AddSeconds(5)
    while ((Get-Date) -lt $prepareDeadline) {
        $line = Read-BoardLine -SerialPort $serialPort
        if ([string]::IsNullOrWhiteSpace($line)) {
            continue
        }

        Write-Host "BOARD> $line"
        if ($line -match "Upload rejected|invalid payload size") {
            throw "Board rejected WAV upload."
        }
        if ($line -match "Ready to receive WAV over serial") {
            break
        }
    }

    $totalChunks = [Math]::Ceiling($hex.Length / $hexCharsPerLine)
    for ($i = 0; $i -lt $hex.Length; $i += $hexCharsPerLine) {
        $count = [Math]::Min($hexCharsPerLine, $hex.Length - $i)
        $serialPort.WriteLine($hex.Substring($i, $count))
        $chunkIndex = [int]($i / $hexCharsPerLine) + 1
        $percent = [Math]::Min(100, [int][Math]::Floor(($chunkIndex * 100.0) / $totalChunks))
        Write-Host -NoNewline ("`rUpload progress: {0}%" -f $percent)

        $chunkDeadline = (Get-Date).AddSeconds(2)
        while ((Get-Date) -lt $chunkDeadline) {
            $line = Read-BoardLine -SerialPort $serialPort
            if ([string]::IsNullOrWhiteSpace($line)) {
                continue
            }

            if ($line -match "Upload failed|Upload rejected|invalid hex payload|payload too large") {
                Write-Host ""
                Write-Host "BOARD> $line"
                throw "Board rejected WAV payload during upload."
            }
            if ($line -eq "Ready") {
                break
            }
            Write-Host ""
            Write-Host "BOARD> $line"
        }
    }
    Write-Host ""

    $serialPort.WriteLine("ENDHEX")
    Write-Host "WAV upload complete."

    $deadline = (Get-Date).AddSeconds($ResponseTimeoutSeconds)
    $sawPlaybackResult = $false
    while ((Get-Date) -lt $deadline) {
        $line = Read-BoardLine -SerialPort $serialPort
        if ($null -eq $line) {
            continue
        }

        if ([string]::IsNullOrWhiteSpace($line)) {
            continue
        }

        Write-Host "BOARD> $line"
        if ($line -match "Playback started|Playback complete|Playback rejected|Playback failed|Upload failed|Upload rejected|WAV parse failed") {
            $sawPlaybackResult = $true
        }
        if ($sawPlaybackResult -and $line -eq "Ready") {
            break
        }
    }
}
finally {
    if ($serialPort.IsOpen) {
        $serialPort.Close()
    }
}
