param(
    [string]$Port = "COM3",
    [int]$Baud = 115200,
    [string]$Model = "qwen-flash",
    [string]$ApiBase = "https://dashscope.aliyuncs.com/compatible-mode/v1",
    [string]$TtsModel = "qwen3-tts-flash",
    [string]$Voice = "Cherry",
    [string]$LanguageType = "Chinese",
    [string]$SystemPrompt = "You are a super goofy robot assistant. You must always reply in Simplified Chinese. Keep replies very short, within 1 to 2 short sentences. Be playful and funny, but still helpful. Do not use emoji, markdown, bullet points, or long action descriptions.",
    [switch]$SpeakReply,
    [string]$ApiKey
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

    throw "Missing API key. Set DASHSCOPE_API_KEY, pass -ApiKey, or create scripts\bailian.local.ps1."
}

function New-Message {
    param(
        [string]$Role,
        [string]$Content
    )

    return @{
        role = $Role
        content = $Content
    }
}

function Invoke-BailianChat {
    param(
        [string]$ApiKeyValue,
        [string]$ApiBaseUrl,
        [string]$ModelName,
        [object[]]$Messages
    )

    $requestUri = ($ApiBaseUrl.TrimEnd("/") + "/chat/completions")
    $payload = @{
        model = $ModelName
        messages = $Messages
    } | ConvertTo-Json -Depth 8
    $payloadBytes = [System.Text.UTF8Encoding]::new($false).GetBytes($payload)
    $request = [System.Net.HttpWebRequest]::Create($requestUri)
    $request.Method = "POST"
    $request.ContentType = "application/json; charset=utf-8"
    $request.Headers["Authorization"] = "Bearer $ApiKeyValue"
    $request.ContentLength = $payloadBytes.Length

    $requestStream = $request.GetRequestStream()
    try {
        $requestStream.Write($payloadBytes, 0, $payloadBytes.Length)
    }
    finally {
        $requestStream.Close()
    }

    try {
        $response = $request.GetResponse()
        $reader = New-Object System.IO.StreamReader($response.GetResponseStream(), [System.Text.UTF8Encoding]::new($false))
        try {
            $json = $reader.ReadToEnd()
        }
        finally {
            $reader.Close()
            $response.Close()
        }
    }
    catch [System.Net.WebException] {
        $errorResponse = $_.Exception.Response
        if ($null -eq $errorResponse) {
            throw
        }

        $reader = New-Object System.IO.StreamReader($errorResponse.GetResponseStream(), [System.Text.UTF8Encoding]::new($false))
        try {
            $errorJson = $reader.ReadToEnd()
        }
        finally {
            $reader.Close()
            $errorResponse.Close()
        }
        throw $errorJson
    }

    return $json | ConvertFrom-Json
}

function Get-AssistantReplyText {
    param([object]$Response)

    if ($null -eq $Response.choices -or $Response.choices.Count -eq 0) {
        throw "Chat response did not include choices."
    }

    $content = $Response.choices[0].message.content
    if ([string]::IsNullOrWhiteSpace($content)) {
        throw "Chat response did not include assistant content."
    }

    return $content.Trim()
}

function Speak-AssistantReply {
    param(
        [string]$Text,
        [string]$SerialPort,
        [int]$SerialBaud,
        [string]$TtsModelName,
        [string]$VoiceName,
        [string]$LanguageName
    )

    $scriptPath = Join-Path $PSScriptRoot "speak_text_with_bailian.ps1"
    if (-not (Test-Path $scriptPath)) {
        throw "Missing helper script: $scriptPath"
    }

    & $scriptPath `
        -Port $SerialPort `
        -Baud $SerialBaud `
        -Model $TtsModelName `
        -Voice $VoiceName `
        -LanguageType $LanguageName `
        -Text $Text
}

$resolvedApiKey = Resolve-ApiKey -ExplicitApiKey $ApiKey
$messages = [System.Collections.Generic.List[object]]::new()
$messages.Add((New-Message -Role "system" -Content $SystemPrompt))

Write-Host "Interactive chat mode. Type text and press Enter."
Write-Host "Commands: /exit, /reset, /speak on, /speak off, /prompt"

$speakEnabled = $SpeakReply.IsPresent

while ($true) {
    $userInput = Read-Host "You"
    if ($null -eq $userInput) {
        continue
    }

    $trimmedInput = $userInput.Trim()
    if ([string]::IsNullOrWhiteSpace($trimmedInput)) {
        continue
    }

    if ($trimmedInput -eq "/exit") {
        break
    }

    if ($trimmedInput -eq "/reset") {
        $messages.Clear()
        $messages.Add((New-Message -Role "system" -Content $SystemPrompt))
        Write-Host "Conversation reset."
        continue
    }

    if ($trimmedInput -eq "/speak on") {
        $speakEnabled = $true
        Write-Host "Speech playback enabled."
        continue
    }

    if ($trimmedInput -eq "/speak off") {
        $speakEnabled = $false
        Write-Host "Speech playback disabled."
        continue
    }

    if ($trimmedInput -eq "/prompt") {
        Write-Host "System prompt:"
        Write-Host $SystemPrompt
        continue
    }

    $messages.Add((New-Message -Role "user" -Content $trimmedInput))

    try {
        $response = Invoke-BailianChat `
            -ApiKeyValue $resolvedApiKey `
            -ApiBaseUrl $ApiBase `
            -ModelName $Model `
            -Messages $messages.ToArray()

        $assistantText = Get-AssistantReplyText -Response $response
        $messages.Add((New-Message -Role "assistant" -Content $assistantText))
        Write-Host ""
        Write-Host "Robot: $assistantText"
        Write-Host ""

        if ($speakEnabled) {
            Speak-AssistantReply `
                -Text $assistantText `
                -SerialPort $Port `
                -SerialBaud $Baud `
                -TtsModelName $TtsModel `
                -VoiceName $Voice `
                -LanguageName $LanguageType
        }
    }
    catch {
        Write-Host "Chat failed: $($_.Exception.Message)"
        if ($messages.Count -gt 1 -and $messages[$messages.Count - 1].role -eq "user") {
            $messages.RemoveAt($messages.Count - 1)
        }
    }
}
