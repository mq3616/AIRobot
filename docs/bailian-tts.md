# Bailian TTS Serial Playback

This project can already receive a WAV file over serial and play it on the ESP32 speaker.
The `scripts/speak_text_with_bailian.ps1` helper connects Aliyun Bailian TTS to that playback path:

`text -> Bailian TTS -> WAV download -> serial upload -> speaker playback`

## Setup

Set your API key in the current PowerShell session:

```powershell
$env:DASHSCOPE_API_KEY = "sk-..."
```

## Single sentence

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\speak_text_with_bailian.ps1 `
  -Port COM3 `
  -Text "Hello, I can speak text through the robot now."
```

## Interactive mode

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\speak_text_with_bailian.ps1 `
  -Port COM3 `
  -Interactive
```

Type `/exit` to quit.

## Options

- `-Voice Cherry`
- `-Model qwen3-tts-flash`
- `-LanguageType Chinese`
- `-ApiBase https://dashscope.aliyuncs.com`
- `-ApiKey sk-...`

If you use the Singapore region, replace `-ApiBase` with `https://dashscope-intl.aliyuncs.com`
and use the matching regional API key.
