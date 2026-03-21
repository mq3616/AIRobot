param(
    [string]$Port = "COM3",
    [int]$Baud = 115200,
    [string]$Model = "qwen3-tts-flash",
    [string]$Voice = "Cherry",
    [string]$LanguageType = "Chinese",
    [string]$ApiBase = "https://dashscope.aliyuncs.com",
    [int]$PlaybackSampleRate = 8000,
    [int]$MaxUploadBytes = 131072,
    [string]$Text,
    [string]$ApiKey,
    [switch]$Interactive
)

$ErrorActionPreference = "Stop"
[Console]::InputEncoding = [System.Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$OutputEncoding = [System.Text.UTF8Encoding]::new($false)

function Resolve-ApiKey {
    param([string]$ExplicitApiKey)

    if (-not [string]::IsNullOrWhiteSpace($ExplicitApiKey)) {
        return $ExplicitApiKey.Trim()
    }

    if (-not [string]::IsNullOrWhiteSpace($env:DASHSCOPE_API_KEY)) {
        return $env:DASHSCOPE_API_KEY.Trim()
    }

    $localConfigPath = Join-Path $PSScriptRoot "bailian.local.ps1"
    if (Test-Path $localConfigPath) {
        . $localConfigPath
        if (-not [string]::IsNullOrWhiteSpace($BailianApiKey)) {
            return $BailianApiKey.Trim()
        }
    }

    throw "Missing API key. Set DASHSCOPE_API_KEY or pass -ApiKey."
}

function Invoke-BailianTts {
    param(
        [string]$ApiKeyValue,
        [string]$ApiBaseUrl,
        [string]$ModelName,
        [string]$VoiceName,
        [string]$LanguageName,
        [string]$Content
    )

    $requestUri = ($ApiBaseUrl.TrimEnd("/") + "/api/v1/services/aigc/multimodal-generation/generation")
    $headers = @{
        Authorization = "Bearer $ApiKeyValue"
        "Content-Type" = "application/json; charset=utf-8"
    }

    $payload = @{
        model = $ModelName
        input = @{
            text = $Content
            voice = $VoiceName
            language_type = $LanguageName
        }
    } | ConvertTo-Json -Depth 6
    $payloadBytes = [System.Text.UTF8Encoding]::new($false).GetBytes($payload)

    return Invoke-RestMethod -Method Post -Uri $requestUri -Headers $headers -Body $payloadBytes
}

function Download-TtsWav {
    param(
        [string]$AudioUrl,
        [string]$TargetPath
    )

    if ([string]::IsNullOrWhiteSpace($AudioUrl)) {
        throw "TTS response did not include an audio URL."
    }

    Invoke-WebRequest -Uri $AudioUrl -OutFile $TargetPath | Out-Null
}

function Remove-StaleTempAudioFiles {
    param([int]$OlderThanHours = 12)

    $cutoff = (Get-Date).AddHours(-1 * $OlderThanHours)
    Get-ChildItem -Path $env:TEMP -Filter "airobot_tts*.wav" -ErrorAction SilentlyContinue |
        Where-Object { $_.LastWriteTime -lt $cutoff } |
        Remove-Item -Force -ErrorAction SilentlyContinue
}

function Get-WavInfo {
    param([byte[]]$Bytes)

    if ($Bytes.Length -lt 44) {
        throw "WAV file is too small."
    }

    $riff = [System.Text.Encoding]::ASCII.GetString($Bytes, 0, 4)
    $wave = [System.Text.Encoding]::ASCII.GetString($Bytes, 8, 4)
    if ($riff -ne "RIFF" -or $wave -ne "WAVE") {
        throw "Unsupported WAV container."
    }

    $offset = 12
    $audioFormat = $null
    $channels = $null
    $sampleRate = $null
    $bitsPerSample = $null
    $dataOffset = $null
    $dataSize = $null

    while (($offset + 8) -le $Bytes.Length) {
        $chunkId = [System.Text.Encoding]::ASCII.GetString($Bytes, $offset, 4)
        $chunkSize = [BitConverter]::ToUInt32($Bytes, $offset + 4)
        $chunkDataOffset = $offset + 8

        if ($chunkId -eq "fmt ") {
            $audioFormat = [BitConverter]::ToUInt16($Bytes, $chunkDataOffset)
            $channels = [BitConverter]::ToUInt16($Bytes, $chunkDataOffset + 2)
            $sampleRate = [BitConverter]::ToUInt32($Bytes, $chunkDataOffset + 4)
            $bitsPerSample = [BitConverter]::ToUInt16($Bytes, $chunkDataOffset + 14)
        } elseif ($chunkId -eq "data") {
            $dataOffset = $chunkDataOffset
            $remainingBytes = $Bytes.Length - $chunkDataOffset
            if ($remainingBytes -lt 0) {
                throw "Invalid WAV data chunk offset."
            }
            $dataSize = [Math]::Min([int64]$chunkSize, [int64]$remainingBytes)
            break
        }

        $offset = $chunkDataOffset + [int]$chunkSize
        if (($chunkSize % 2) -eq 1) {
            $offset += 1
        }
    }

    if ($null -eq $audioFormat -or $null -eq $dataOffset) {
        throw "Incomplete WAV file."
    }

    return [pscustomobject]@{
        AudioFormat   = $audioFormat
        Channels      = $channels
        SampleRate    = $sampleRate
        BitsPerSample = $bitsPerSample
        DataOffset    = [int]$dataOffset
        DataSize      = [int]$dataSize
    }
}

function Write-WavPcm16Mono {
    param(
        [int16[]]$Samples,
        [int]$SampleRate,
        [string]$TargetPath
    )

    $dataSize = $Samples.Length * 2
    $riffSize = 36 + $dataSize
    $bytes = New-Object byte[] (44 + $dataSize)
    [System.Text.Encoding]::ASCII.GetBytes("RIFF").CopyTo($bytes, 0)
    [BitConverter]::GetBytes([uint32]$riffSize).CopyTo($bytes, 4)
    [System.Text.Encoding]::ASCII.GetBytes("WAVE").CopyTo($bytes, 8)
    [System.Text.Encoding]::ASCII.GetBytes("fmt ").CopyTo($bytes, 12)
    [BitConverter]::GetBytes([uint32]16).CopyTo($bytes, 16)
    [BitConverter]::GetBytes([uint16]1).CopyTo($bytes, 20)
    [BitConverter]::GetBytes([uint16]1).CopyTo($bytes, 22)
    [BitConverter]::GetBytes([uint32]$SampleRate).CopyTo($bytes, 24)
    [BitConverter]::GetBytes([uint32]($SampleRate * 2)).CopyTo($bytes, 28)
    [BitConverter]::GetBytes([uint16]2).CopyTo($bytes, 32)
    [BitConverter]::GetBytes([uint16]16).CopyTo($bytes, 34)
    [System.Text.Encoding]::ASCII.GetBytes("data").CopyTo($bytes, 36)
    [BitConverter]::GetBytes([uint32]$dataSize).CopyTo($bytes, 40)

    for ($i = 0; $i -lt $Samples.Length; $i++) {
        [BitConverter]::GetBytes([int16]$Samples[$i]).CopyTo($bytes, 44 + ($i * 2))
    }

    [System.IO.File]::WriteAllBytes($TargetPath, $bytes)
}

function Optimize-WavForPlayback {
    param(
        [string]$InputPath,
        [string]$OutputPath,
        [int]$TargetSampleRate
    )

    $bytes = [System.IO.File]::ReadAllBytes($InputPath)
    $info = Get-WavInfo -Bytes $bytes

    if ($info.AudioFormat -ne 1 -or $info.BitsPerSample -ne 16) {
        [System.IO.File]::Copy($InputPath, $OutputPath, $true)
        return
    }

    $sourceFrameSize = [int]($info.Channels * 2)
    $sourceFrameCount = [int]($info.DataSize / $sourceFrameSize)
    if ($sourceFrameCount -le 0) {
        throw "WAV data chunk is empty."
    }

    $monoSamples = New-Object 'System.Int16[]' $sourceFrameCount
    for ($frame = 0; $frame -lt $sourceFrameCount; $frame++) {
        $frameOffset = $info.DataOffset + ($frame * $sourceFrameSize)
        if (($frameOffset + $sourceFrameSize) -gt $bytes.Length) {
            break
        }
        if ($info.Channels -eq 1) {
            $monoSamples[$frame] = [BitConverter]::ToInt16($bytes, $frameOffset)
        } else {
            $sum = 0
            for ($ch = 0; $ch -lt $info.Channels; $ch++) {
                $sum += [BitConverter]::ToInt16($bytes, $frameOffset + ($ch * 2))
            }
            $monoSamples[$frame] = [int16]([math]::Round($sum / $info.Channels))
        }
    }

    if ($sourceFrameCount -gt 0) {
        $actualFrameCount = [Math]::Min($sourceFrameCount, [int](($bytes.Length - $info.DataOffset) / $sourceFrameSize))
        if ($actualFrameCount -le 0) {
            throw "WAV PCM payload is truncated."
        }
        if ($actualFrameCount -lt $sourceFrameCount) {
            $sourceFrameCount = $actualFrameCount
            $trimmedSamples = New-Object 'System.Int16[]' $sourceFrameCount
            [Array]::Copy($monoSamples, $trimmedSamples, $sourceFrameCount)
            $monoSamples = $trimmedSamples
        }
    }

    $effectiveTargetRate = $TargetSampleRate
    if ($effectiveTargetRate -le 0 -or $effectiveTargetRate -gt $info.SampleRate) {
        $effectiveTargetRate = [int]$info.SampleRate
    }

    if ($info.Channels -eq 1 -and $effectiveTargetRate -eq $info.SampleRate) {
        [System.IO.File]::Copy($InputPath, $OutputPath, $true)
        return
    }

    $targetFrameCount = [math]::Max(1, [int][math]::Round($sourceFrameCount * $effectiveTargetRate / $info.SampleRate))
    $targetSamples = New-Object 'System.Int16[]' $targetFrameCount

    for ($i = 0; $i -lt $targetFrameCount; $i++) {
        $sourcePos = $i * $info.SampleRate / $effectiveTargetRate
        $leftIndex = [int][math]::Floor($sourcePos)
        if ($leftIndex -ge ($sourceFrameCount - 1)) {
            $targetSamples[$i] = $monoSamples[$sourceFrameCount - 1]
            continue
        }

        $rightIndex = $leftIndex + 1
        $fraction = $sourcePos - $leftIndex
        $leftValue = [double]$monoSamples[$leftIndex]
        $rightValue = [double]$monoSamples[$rightIndex]
        $interpolated = $leftValue + (($rightValue - $leftValue) * $fraction)
        $targetSamples[$i] = [int16][math]::Round($interpolated)
    }

    Write-WavPcm16Mono -Samples $targetSamples -SampleRate $effectiveTargetRate -TargetPath $OutputPath
}

function Get-WavFileInfo {
    param([string]$Path)

    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $info = Get-WavInfo -Bytes $bytes
    return [pscustomobject]@{
        SizeBytes     = (Get-Item $Path).Length
        AudioFormat   = $info.AudioFormat
        Channels      = $info.Channels
        SampleRate    = $info.SampleRate
        BitsPerSample = $info.BitsPerSample
        DataSize      = $info.DataSize
    }
}

function Ensure-WavWithinLimit {
    param(
        [string]$SourcePath,
        [string]$OutputPath,
        [int]$PreferredSampleRate,
        [int]$UploadByteLimit
    )

    $candidateRates = New-Object System.Collections.Generic.List[int]
    foreach ($rate in @($PreferredSampleRate, 6000, 4000)) {
        if ($rate -gt 0 -and -not $candidateRates.Contains($rate)) {
            $candidateRates.Add($rate)
        }
    }

    foreach ($rate in $candidateRates) {
        Optimize-WavForPlayback -InputPath $SourcePath -OutputPath $OutputPath -TargetSampleRate $rate
        $info = Get-WavFileInfo -Path $OutputPath
        Write-Host ("Prepared playback WAV: {0} bytes, {1} Hz, {2} ch, {3} bit" -f $info.SizeBytes, $info.SampleRate, $info.Channels, $info.BitsPerSample)
        if ($info.SizeBytes -le $UploadByteLimit) {
            return
        }
    }

    $finalInfo = Get-WavFileInfo -Path $OutputPath
    throw ("Playback WAV still too large: {0} bytes exceeds limit {1} bytes" -f $finalInfo.SizeBytes, $UploadByteLimit)
}

function Send-WavToBoard {
    param(
        [string]$SerialPort,
        [int]$SerialBaud,
        [string]$WavPath
    )

    $scriptPath = Join-Path $PSScriptRoot "play_wav_over_serial.ps1"
    if (-not (Test-Path $scriptPath)) {
        throw "Missing helper script: $scriptPath"
    }

    & $scriptPath -Port $SerialPort -Baud $SerialBaud -InputPath $WavPath
}

function Speak-Once {
    param(
        [string]$ApiKeyValue,
        [string]$ApiBaseUrl,
        [string]$ModelName,
        [string]$VoiceName,
        [string]$LanguageName,
        [string]$SerialPort,
        [int]$SerialBaud,
        [string]$Content
    )

    $trimmedText = $Content.Trim()
    if ([string]::IsNullOrWhiteSpace($trimmedText)) {
        Write-Host "Skipped empty text."
        return
    }

    $trimmedText = $trimmedText -replace '[\r\n]+', ' '
    $trimmedText = $trimmedText -replace '\*', ''
    $trimmedText = $trimmedText -replace '[^\u0000-\u007F\u4E00-\u9FFF\u3000-\u303F\uFF00-\uFFEF]+', ' '
    $trimmedText = $trimmedText -replace '\s{2,}', ' '
    $trimmedText = $trimmedText.Trim()
    Remove-StaleTempAudioFiles

    $downloadedFile = Join-Path $env:TEMP ("airobot_tts_src_" + [guid]::NewGuid().ToString("N") + ".wav")
    $playbackFile = Join-Path $env:TEMP ("airobot_tts_" + [guid]::NewGuid().ToString("N") + ".wav")
    try {
        Write-Host "Synthesizing with Bailian TTS..."
        $response = Invoke-BailianTts `
            -ApiKeyValue $ApiKeyValue `
            -ApiBaseUrl $ApiBaseUrl `
            -ModelName $ModelName `
            -VoiceName $VoiceName `
            -LanguageName $LanguageName `
            -Content $trimmedText

        $audioUrl = $response.output.audio.url
        if ([string]::IsNullOrWhiteSpace($audioUrl)) {
            $errorCode = $response.code
            $errorMessage = $response.message
            throw "TTS request failed: $errorCode $errorMessage"
        }

        Download-TtsWav -AudioUrl $audioUrl -TargetPath $downloadedFile
        Ensure-WavWithinLimit -SourcePath $downloadedFile -OutputPath $playbackFile -PreferredSampleRate $PlaybackSampleRate -UploadByteLimit $MaxUploadBytes
        Send-WavToBoard -SerialPort $SerialPort -SerialBaud $SerialBaud -WavPath $playbackFile
    }
    finally {
        if (Test-Path $downloadedFile) {
            Remove-Item $downloadedFile -Force -ErrorAction SilentlyContinue
        }
        if (Test-Path $playbackFile) {
            Remove-Item $playbackFile -Force -ErrorAction SilentlyContinue
        }
    }
}

$resolvedApiKey = Resolve-ApiKey -ExplicitApiKey $ApiKey
$runInteractive = $Interactive.IsPresent -or [string]::IsNullOrWhiteSpace($Text)

if ($runInteractive) {
    Write-Host "Interactive mode. Type text and press Enter. Type /exit to quit."
    while ($true) {
        $line = Read-Host "You"
        if ($null -eq $line) {
            continue
        }

        $trimmedLine = $line.Trim()
        if ($trimmedLine -eq "/exit") {
            break
        }

        Speak-Once `
            -ApiKeyValue $resolvedApiKey `
            -ApiBaseUrl $ApiBase `
            -ModelName $Model `
            -VoiceName $Voice `
            -LanguageName $LanguageType `
            -SerialPort $Port `
            -SerialBaud $Baud `
            -Content $trimmedLine
    }
} else {
    Speak-Once `
        -ApiKeyValue $resolvedApiKey `
        -ApiBaseUrl $ApiBase `
        -ModelName $Model `
        -VoiceName $Voice `
        -LanguageName $LanguageType `
        -SerialPort $Port `
        -SerialBaud $Baud `
        -Content $Text
}
