param(
    [string]$Port = "COM3",
    [int]$Baud = 115200,
    [ValidateSet("left", "right")]
    [string]$Channel = "left",
    [string]$OutputPath = ""
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot

if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $OutputPath = Join-Path $projectRoot "recording_${Channel}_$timestamp.wav"
}

$command = if ($Channel -eq "left") { "dl" } else { "dr" }

$serialPort = New-Object System.IO.Ports.SerialPort $Port, $Baud, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
$serialPort.NewLine = "`n"
$serialPort.ReadTimeout = 30000
$serialPort.WriteTimeout = 5000
$serialPort.DtrEnable = $true
$serialPort.RtsEnable = $true

$hexLines = New-Object System.Collections.Generic.List[string]
$expectedSize = 0
$captureStarted = $false

try {
    $serialPort.Open()
    Start-Sleep -Milliseconds 1200
    $serialPort.DiscardInBuffer()
    $serialPort.DiscardOutBuffer()

    $deadline = (Get-Date).AddSeconds(45)
    $nextCommandAt = Get-Date
    while ((Get-Date) -lt $deadline) {
        if ((Get-Date) -ge $nextCommandAt) {
            Write-Host "Sending '$command' to $Port..."
            $serialPort.WriteLine($command)
            $nextCommandAt = (Get-Date).AddSeconds(3)
        }

        try {
            $line = $serialPort.ReadLine().Trim()
        }
        catch [TimeoutException] {
            continue
        }

        if (-not $captureStarted) {
            if ($line -eq "BEGIN_WAV_HEX") {
                $captureStarted = $true
                Write-Host "Capture started"
            }
            continue
        }

        if ($line -like "SIZE:*") {
            $expectedSize = [int]($line.Substring(5))
            Write-Host "Expected size: $expectedSize bytes"
            continue
        }

        if ($line -eq "END_WAV_HEX") {
            break
        }

        if ($line.Length -gt 0) {
            $hexLines.Add($line)
        }
    }

    if (-not $captureStarted) {
        throw "Did not receive BEGIN_WAV_HEX from device"
    }

    if ($hexLines.Count -eq 0) {
        throw "No WAV payload received from device"
    }

    $hex = ($hexLines -join "")
    if (($hex.Length % 2) -ne 0) {
        throw "Received odd-length hex payload"
    }

    $bytes = New-Object byte[] ($hex.Length / 2)
    for ($i = 0; $i -lt $bytes.Length; $i++) {
        $bytes[$i] = [Convert]::ToByte($hex.Substring($i * 2, 2), 16)
    }

    if ($expectedSize -gt 0 -and $bytes.Length -ne $expectedSize) {
        throw "Size mismatch. Expected $expectedSize bytes, got $($bytes.Length)"
    }

    [System.IO.File]::WriteAllBytes($OutputPath, $bytes)
    Write-Host "Saved WAV to $OutputPath"
}
finally {
    if ($serialPort.IsOpen) {
        $serialPort.Close()
    }
}
