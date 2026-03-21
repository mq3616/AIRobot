# Bailian Chat + TTS

This helper provides a full typed conversation loop on the PC side:

`you type -> qwen-flash replies -> qwen3-tts-flash speaks through the ESP32 speaker`

## Run

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\chat_with_bailian.ps1 `
  -Port COM3 `
  -SpeakReply
```

## Defaults

- Chat model: `qwen-flash`
- TTS model: `qwen3-tts-flash`
- Voice: `Cherry`
- Region base URL: `https://dashscope.aliyuncs.com/compatible-mode/v1`

## Commands

- `/exit` quit
- `/reset` clear conversation history
- `/speak on` enable robot voice playback
- `/speak off` disable robot voice playback
- `/prompt` print the active system prompt

## Notes

The script reads the API key from this order:

1. `-ApiKey`
2. `DASHSCOPE_API_KEY`
3. `scripts\bailian.local.ps1`

If you use the Singapore region, change the chat base URL to:

```powershell
-ApiBase https://dashscope-intl.aliyuncs.com/compatible-mode/v1
```
