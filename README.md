# AIRobot

[中文](./README.md) | [English](./README_EN.md)

## 最近更新

本轮迭代新增了“手机控制 AIRobot”的完整链路，核心更新如下：

- 固件新增内置 Web 控制页与 HTTP API
  - `GET /api/status`
  - `POST /api/command`
  - `POST /api/volume`
  - `POST /api/accompaniment`
  - `POST /api/melody/play`
  - `POST /api/melody/stop`
  - `POST /api/tts`
  - `POST /api/chat`
  - `POST /api/asr-chat`
- 设备接入 `mDNS`，局域网内可通过 `http://airobot.local/` 访问 Web 控制页
- 串口命令和 HTTP 控制共用同一套设备侧执行逻辑，避免两套行为分叉
- 新增 Android 控制端工程 [`airobot_mobile`](./airobot_mobile)
  - 自动发现 AIRobot
  - 自动回退到可用 IP
  - 长按录音、松开发送
  - 文本聊天、TTS、音乐控制、音量调节
  - 显示连接状态与语音阶段
- 语音链路新增连续对话上下文，语音对话不再总是单轮问答
- LLM 回复策略已收敛为“尽量简短”，更适合机器人播报

### 语音链路现状

当前手机语音链路为：

`Android App 录音 -> AIRobot /api/asr-chat -> Bailian ASR -> Chat -> TTS -> 机器人扬声器`

为提高稳定性，当前已做这些保护：

- Android 端录音时长限制为 5 秒以内
- 上传音频使用压缩格式，避免原始 WAV 过大导致设备端内存压力
- 固件侧会检查上传大小和可用堆内存，超限时直接拒绝而不是硬扛
- 上传缓冲在请求结束后立即释放

### 当前已知限制

- Android 端 `.local` 解析不稳定时，会自动回退到局域网 IP
- Web 页面在部分手机浏览器里对麦克风权限支持不稳定，因此手机端优先推荐使用 Android App
- 百炼 ASR 的输入格式存在兼容边界，当前仍在持续联调；若语音识别失败，请优先查看串口/控制台输出的 ASR 原始错误

AIRobot 是一个基于 ESP32-S3 的嵌入式 AI 语音机器人项目，目标是把语音交互、表情反馈、音乐播放和桌面调试工具整合到同一套可落地的机器人原型中。

## 项目背景

这个项目面向“轻量本地硬件 + 云端 AI 能力”的机器人实践场景。相比纯软件语音助手，AIRobot 更强调真实设备上的交互闭环：

- 板端负责音频采集、播放、串口控制、显示驱动和本地状态管理
- 云端负责聊天、语音合成等高成本能力
- 桌面端提供可视化串口控制台，降低烧录、监视日志和发送指令的门槛

项目的核心思路不是先做一个大而全的平台，而是先把“能说、能播、能调、能迭代”的机器人基础链路打通。

## 项目简介

当前 AIRobot 主要包含以下部分：

- `ESP32-S3 + Arduino + PlatformIO` 固件工程
- 基于串口命令的语音、音乐、状态控制接口
- 百炼 Chat 与 TTS 接入
- OLED 表情/状态显示
- 针对小型扬声器持续调优的音乐播放系统
- 暗黑风格的桌面串口控制台 `AIRobot Console`

目前已支持的典型能力包括：

- 发送 `ask <text>` 获取聊天回复并自动播报
- 发送 `say <text>` 直接调用 TTS 播放
- 串口播放内置旋律，并切换钢琴 / 八音盒音色
- 在播放过程中动态调整音量、伴奏开关等参数
- 通过桌面 GUI 完成串口连接、日志查看、命令发送和固件烧录

## 当前硬件清单

你现在这套 AIRobot 工程所使用的核心硬件为：

- `ESP32-S3 DevKitC-1-N8`
- `INMP441` I2S 麦克风
- `MAX98357A` I2S 功放模块
- `0.96"` `128x64` I2C OLED 显示屏
- 小型扬声器
- USB 数据线

当前默认引脚定义见 [include/app_config.h](./include/app_config.h)：

- 麦克风：`GPIO4 / GPIO5 / GPIO6`
- OLED：`GPIO8 / GPIO9`
- 功放：`GPIO7 / GPIO15 / GPIO16`

## 快速开始

### 1. 准备环境

- 安装 `Python`
- 安装 `PlatformIO`
- 连接开发板到电脑

### 2. 配置 WiFi 和 API Key

仓库不会提交你的真实密钥。当前已经忽略了这些本地文件：

- `include/app_secrets.h`
- `scripts/bailian.local.ps1`
- `.serial_console_gui.json`

推荐的配置方式是通过环境变量注入，构建脚本 [scripts/inject_secrets.py](./scripts/inject_secrets.py) 会自动读取：

- `WIFI_SSID`
- `WIFI_PASSWORD`
- `OPENAI_API_KEY`

PowerShell 示例：

```powershell
$env:WIFI_SSID="your-ssid"
$env:WIFI_PASSWORD="your-password"
$env:OPENAI_API_KEY="sk-..."
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" -m platformio run -e esp32-s3-devkit
```

如果你只想在本机临时使用，也可以复制 [include/app_secrets.example.h](./include/app_secrets.example.h) 为 `include/app_secrets.h` 并填写本地配置。这个文件已加入 `.gitignore`，不会被提交。

### 3. 编译项目

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" -m platformio run -e esp32-s3-devkit
```

## 烧录

### 通过 PlatformIO 命令行

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" -m platformio run -e esp32-s3-devkit -t upload --upload-port COM3
```

### 通过 AIRobot Console

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\open_serial_console_gui.ps1
```

GUI 支持：

- 自动连接串口
- 发送命令
- 查看日志
- 关闭串口后自动烧录
- 烧录成功后自动重开控制台

详细说明见 [docs/serial-console-gui.md](./docs/serial-console-gui.md)。

## 串口命令

常用串口命令包括：

- `help`：查看帮助
- `wifi`：查看 WiFi 状态
- `mem`：查看内存状态
- `profile`：查看当前用户 / 机器人资料
- `beep`：播放测试音
- `ask <text>`：调用聊天并自动播报
- `say <text>`：直接调用 TTS
- `volume`：查看当前音量
- `volume <0-100>`：设置播放音量
- `accomp`：查看八音盒伴奏状态
- `accomp on`：开启八音盒伴奏
- `accomp off`：关闭八音盒伴奏
- `melody canon musicbox`
- `melody tori musicbox`
- `melody truth musicbox`
- `melody juebieshu musicbox`
- `melody qifeng musicbox`
- `melody sinian musicbox`
- `melody stop`

更完整的交互逻辑可参考 [src/main.cpp](./src/main.cpp)。

## 技术路线

- 主控：ESP32-S3 DevKitC-1
- 固件框架：Arduino
- 构建工具：PlatformIO
- 显示：Adafruit SSD1306 + RoboEyes
- 音频：I2S 输入 / 输出链路
- 桌面工具：Python + Tkinter + pyserial

## 目录结构

- [src](./src)
- [include](./include)
- [scripts](./scripts)
- [assets](./assets)
- [docs](./docs)

## 配套文档

- 串口控制台说明：[docs/serial-console-gui.md](./docs/serial-console-gui.md)
- 百炼 Chat 接入说明：[docs/bailian-chat.md](./docs/bailian-chat.md)
- 百炼 TTS 接入说明：[docs/bailian-tts.md](./docs/bailian-tts.md)

## 当前定位

AIRobot 目前更适合作为：

- AI 语音机器人原型
- 嵌入式语音交互实验项目
- ESP32-S3 音频 / 串口 / 桌面联调样例
- 后续扩展实体机器人功能的基础工程

## 说明

- 本项目仍在快速迭代，固件行为、曲目、桌面工具和音色会持续调整
- 如果你需要英文介绍，请查看 [README_EN.md](./README_EN.md)
