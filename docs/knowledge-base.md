# AI 语音机器人知识库

## 1. 项目概述

本项目是一个基于 ESP32-S3 的 AI 语音助手，使用 PlatformIO + Arduino 框架开发。

主流程如下：

1. 通过 INMP441 麦克风采集音频
2. 将 WAV 音频发送到 OpenAI ASR 接口
3. 获取识别文本
4. 将文本发送到 OpenAI 对话接口
5. 获取回复文本
6. 将回复文本发送到 OpenAI TTS 接口
7. 获取 WAV 音频
8. 通过 MAX98357A 和扬声器播放

项目同时支持无硬件测试模式，可以在麦克风和喇叭未到位之前先验证云端链路。

## 2. 硬件映射

### 麦克风：INMP441

- `3V3 -> VDD`
- `GND -> GND`
- `GPIO4 -> WS`
- `GPIO5 -> SCK`
- `GPIO6 -> SD`

### 功放：MAX98357A

- `3V3 -> VIN`
- `GND -> GND`
- `GPIO7 -> DIN`
- `GPIO15 -> BCLK`
- `GPIO16 -> LRC`

### 主控板

- ESP32-S3 DevKit

## 3. 项目结构

### 构建相关

- [platformio.ini](/d:/Projects/AIRobot/platformio.ini)
- [scripts/inject_secrets.py](/d:/Projects/AIRobot/scripts/inject_secrets.py)

### 源码

- [src/main.cpp](/d:/Projects/AIRobot/src/main.cpp)
- [src/wifi.cpp](/d:/Projects/AIRobot/src/wifi.cpp)
- [src/audio.cpp](/d:/Projects/AIRobot/src/audio.cpp)
- [src/asr.cpp](/d:/Projects/AIRobot/src/asr.cpp)
- [src/chat.cpp](/d:/Projects/AIRobot/src/chat.cpp)
- [src/tts.cpp](/d:/Projects/AIRobot/src/tts.cpp)
- [src/http_client.cpp](/d:/Projects/AIRobot/src/http_client.cpp)

### 头文件

- [include/app_config.h](/d:/Projects/AIRobot/include/app_config.h)
- [include/app_wifi.h](/d:/Projects/AIRobot/include/app_wifi.h)
- [include/audio.h](/d:/Projects/AIRobot/include/audio.h)
- [include/asr.h](/d:/Projects/AIRobot/include/asr.h)
- [include/chat.h](/d:/Projects/AIRobot/include/chat.h)
- [include/tts.h](/d:/Projects/AIRobot/include/tts.h)
- [include/http_client.h](/d:/Projects/AIRobot/include/http_client.h)

### 本地配置 / 测试资源

- [include/app_secrets.example.h](/d:/Projects/AIRobot/include/app_secrets.example.h)
- [include/app_secrets.h](/d:/Projects/AIRobot/include/app_secrets.h)
- [include/test_wav_sample.h](/d:/Projects/AIRobot/include/test_wav_sample.h)

## 4. 运行模式

### 真实硬件模式

串口命令：

- `r`

执行流程：

- 麦克风录音 -> ASR -> Chat -> TTS -> 播放

前提条件：

- WiFi 已连接
- 麦克风硬件已接好
- 功放和喇叭已接好
- OpenAI API Key 有效

### 模拟文本模式

串口命令：

- `s`
- `s hello`

执行流程：

- 模拟文本 -> Chat -> TTS

用途：

- 在没有麦克风时先测试网络链路
- 在没有音频输入时验证 Chat 和 TTS

### 内置 WAV 模式

串口命令：

- `w`

执行流程：

- 内置 WAV -> ASR -> Chat -> TTS

用途：

- 在没有硬件时测试 ASR、Chat、TTS 全链路
- 验证完整云端流程，排除实时录音部分影响

## 5. 串口日志摘要

每次执行流程结束后，程序会输出统一格式的摘要块：

- `Flow`
- `Status`
- `ASR Text`
- `AI Reply`
- `TTS WAV`

对应实现位于 [src/main.cpp](/d:/Projects/AIRobot/src/main.cpp)。

## 6. OpenAI 接入说明

### ASR

实现文件：[src/asr.cpp](/d:/Projects/AIRobot/src/asr.cpp)

- 接口：`POST /v1/audio/transcriptions`
- 上传方式：`multipart/form-data`
- 文件格式：WAV
- 默认模型：`gpt-4o-mini-transcribe`

### Chat

实现文件：[src/chat.cpp](/d:/Projects/AIRobot/src/chat.cpp)

- 接口：`POST /v1/responses`
- 默认模型：`gpt-4.1-mini`

### TTS

实现文件：[src/tts.cpp](/d:/Projects/AIRobot/src/tts.cpp)

- 接口：`POST /v1/audio/speech`
- 输出格式：WAV
- 默认模型：`gpt-4o-mini-tts`
- 默认音色：`alloy`

### HTTP 层

实现文件：[src/http_client.cpp](/d:/Projects/AIRobot/src/http_client.cpp)

能力包括：

- JSON POST
- 二进制 POST
- multipart 表单上传
- HTTPS 安全连接

## 7. 配置项

核心配置位于 [include/app_config.h](/d:/Projects/AIRobot/include/app_config.h)。

主要内容包括：

- OpenAI 基础 URL
- ASR / Chat / TTS 模型名
- TTS 音色
- 录音时长
- 音频采样率
- 硬件启用开关
- 测试模式相关开关
- I2S 引脚定义

## 8. 密钥管理

不建议把密钥直接提交到仓库。

当前方案：

1. [platformio.ini](/d:/Projects/AIRobot/platformio.ini) 通过 [scripts/inject_secrets.py](/d:/Projects/AIRobot/scripts/inject_secrets.py) 注入配置
2. 构建脚本从系统环境变量读取值
3. 在编译阶段注入为宏
4. [include/app_config.h](/d:/Projects/AIRobot/include/app_config.h) 优先使用这些宏

支持的环境变量：

- `WIFI_SSID`
- `WIFI_PASSWORD`
- `OPENAI_API_KEY`

PowerShell 示例：

```powershell
$env:WIFI_SSID="your-ssid"
$env:WIFI_PASSWORD="your-password"
$env:OPENAI_API_KEY="sk-..."
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run
```

兜底方式：

- 可以使用本地 [include/app_secrets.h](/d:/Projects/AIRobot/include/app_secrets.h)
- 该文件已加入 `.gitignore`

## 9. 音频细节

录音参数：

- 采样率：`16000 Hz`
- 单声道
- `16-bit PCM`
- 固件中自动封装为 WAV

播放约束：

- 播放前先解析 WAV 头
- 当前只支持单声道 16-bit WAV
- 通过 MAX98357A 进行 I2S 播放

## 10. 已知限制

### 1. 内置测试 WAV 会增加固件体积

[include/test_wav_sample.h](/d:/Projects/AIRobot/include/test_wav_sample.h) 把测试音频直接编进了固件。

影响：

- Flash 占用会明显高于最小版本

### 2. 当前没有唤醒词或按键触发

目前使用串口命令作为触发方式。

### 3. 当前没有本地持久化存储

尚未加入 SPIFFS / LittleFS 来缓存音频或日志。

## 11. 推荐联调流程

### 没有硬件时

1. 设置环境变量
2. 编译项目
3. 如果有板子则烧录
4. 使用 `s` 测试 Chat + TTS
5. 使用 `w` 测试 ASR + Chat + TTS

### 有硬件时

1. 接好麦克风和功放
2. 确认 WiFi 连接成功
3. 输入 `r`
4. 在串口监视器中查看摘要输出

## 12. 常见修改点

### 更换模型

修改 [include/app_config.h](/d:/Projects/AIRobot/include/app_config.h)：

- `OPENAI_ASR_MODEL`
- `OPENAI_CHAT_MODEL`
- `OPENAI_TTS_MODEL`

### 更换 TTS 音色

修改 [include/app_config.h](/d:/Projects/AIRobot/include/app_config.h)：

- `OPENAI_TTS_VOICE`

### 临时关闭音频硬件

修改 [include/app_config.h](/d:/Projects/AIRobot/include/app_config.h)：

- `ENABLE_AUDIO_HARDWARE = false`

### 在测试模式下启用播放

修改 [include/app_config.h](/d:/Projects/AIRobot/include/app_config.h)：

- `ENABLE_SIMULATION_PLAYBACK`
- `ENABLE_TEST_WAV_PLAYBACK`

## 13. 当前构建状态

当前状态：

- 项目可正常通过 PlatformIO 编译
- 目标板：`esp32-s3-devkitc-1`
- 框架：Arduino

## 14. 后续可扩展方向

- 增加实体按键触发
- 增加 VAD 语音活动检测
- 增加流式语音交互或 Realtime 模式
- 改成文件式测试音频，减少固件体积
- 增加 LittleFS 缓存最近一次回复音频
- 增加更完善的重试、超时和错误恢复机制
