#pragma once

#include <Arduino.h>

#if __has_include("app_secrets.h")
#include "app_secrets.h"
#endif

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

#ifndef OPENAI_API_KEY
#define OPENAI_API_KEY ""
#endif

namespace AppConfig {

constexpr uint32_t AUDIO_SAMPLE_RATE = 16000;
constexpr uint8_t AUDIO_BITS_PER_SAMPLE = 16;
constexpr uint8_t AUDIO_CHANNELS = 1;

constexpr char WIFI_SSID_VALUE[] = WIFI_SSID;
constexpr char WIFI_PASSWORD_VALUE[] = WIFI_PASSWORD;
constexpr char CLOUD_API_KEY_VALUE[] = OPENAI_API_KEY;
constexpr char BAILIAN_API_HOST[] = "dashscope.aliyuncs.com";
constexpr uint16_t BAILIAN_API_PORT = 443;
constexpr char BAILIAN_TTS_PATH[] =
    "/api/v1/services/aigc/multimodal-generation/generation";
constexpr char BAILIAN_CHAT_PATH[] = "/compatible-mode/v1/chat/completions";
constexpr char BAILIAN_CHAT_MODEL[] = "qwen-flash";
constexpr char BAILIAN_ASR_MODEL[] = "qwen3-asr-flash";
constexpr char BAILIAN_TTS_MODEL[] = "qwen3-tts-flash";
constexpr char BAILIAN_TTS_VOICE[] = "Cherry";
constexpr char BAILIAN_TTS_LANGUAGE[] = "Chinese";
constexpr uint32_t HTTPS_CONNECT_TIMEOUT_MS = 15000;
constexpr uint32_t HTTPS_RESPONSE_TIMEOUT_MS = 20000;
constexpr size_t TTS_TEXT_LIMIT = 400;
constexpr size_t TTS_HEADER_BUFFER_LIMIT = 512;
constexpr size_t TTS_JSON_BUFFER_LIMIT = 4096;
constexpr size_t CHAT_TEXT_LIMIT = 400;
constexpr size_t CHAT_JSON_BUFFER_LIMIT = 8192;
constexpr size_t ASR_AUDIO_DATA_URL_LIMIT = 128 * 1024;
constexpr size_t ASR_JSON_BUFFER_LIMIT = 12288;
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000;
constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 5000;
constexpr uint32_t RECORD_DURATION_MS = 4000;
constexpr long TIME_GMT_OFFSET_SECONDS = 8 * 3600;
constexpr int TIME_DAYLIGHT_OFFSET_SECONDS = 0;

constexpr gpio_num_t MIC_WS_PIN = GPIO_NUM_4;
constexpr gpio_num_t MIC_SCK_PIN = GPIO_NUM_5;
constexpr gpio_num_t MIC_SD_PIN = GPIO_NUM_6;
constexpr gpio_num_t OLED_SDA_PIN = GPIO_NUM_8;
constexpr gpio_num_t OLED_SCL_PIN = GPIO_NUM_9;

constexpr gpio_num_t SPK_DIN_PIN = GPIO_NUM_7;
constexpr gpio_num_t SPK_BCLK_PIN = GPIO_NUM_15;
constexpr gpio_num_t SPK_LRC_PIN = GPIO_NUM_16;

constexpr size_t I2S_DMA_BUFFER_COUNT = 6;
constexpr size_t I2S_DMA_BUFFER_LENGTH = 256;
constexpr size_t SERIAL_WAV_UPLOAD_LIMIT = 128 * 1024;
constexpr size_t SERIAL_WAV_RECOMMENDED_LIMIT = SERIAL_WAV_UPLOAD_LIMIT;
constexpr uint8_t OLED_I2C_ADDRESS = 0x3C;
constexpr uint8_t OLED_I2C_FALLBACK_ADDRESS = 0x3D;
constexpr uint8_t OLED_WIDTH = 128;
constexpr uint8_t OLED_HEIGHT = 64;

}  // namespace AppConfig
