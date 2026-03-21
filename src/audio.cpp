#include "audio.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include <driver/i2s.h>

#include "app_config.h"

namespace {

constexpr i2s_port_t kSpeakerI2sPort = I2S_NUM_1;
constexpr i2s_port_t kMicI2sPort = I2S_NUM_0;
constexpr size_t kWavHeaderSize = 44;
constexpr uint8_t kDefaultOutputVolumePercent = 70;
constexpr int kMicInputShift = 14;
constexpr float kPi = 3.14159265358979323846f;
MicChannel g_mic_channel = MicChannel::Left;
bool g_stream_active = false;
uint32_t g_stream_sample_rate = 0;
uint8_t g_output_volume_percent = kDefaultOutputVolumePercent;

bool install_speaker_i2s() {
  const i2s_config_t config = {
      .mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX),
      .sample_rate = AppConfig::AUDIO_SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = AppConfig::I2S_DMA_BUFFER_COUNT,
      .dma_buf_len = AppConfig::I2S_DMA_BUFFER_LENGTH,
      .use_apll = false,
      .tx_desc_auto_clear = true,
      .fixed_mclk = 0,
  };

  const i2s_pin_config_t pin_config = {
      .bck_io_num = AppConfig::SPK_BCLK_PIN,
      .ws_io_num = AppConfig::SPK_LRC_PIN,
      .data_out_num = AppConfig::SPK_DIN_PIN,
      .data_in_num = I2S_PIN_NO_CHANGE,
  };

  esp_err_t err = i2s_driver_install(kSpeakerI2sPort, &config, 0, nullptr);
  if (err != ESP_OK) {
    Serial.printf("Speaker I2S install failed: %d\n", err);
    return false;
  }

  err = i2s_set_pin(kSpeakerI2sPort, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("Speaker I2S pin setup failed: %d\n", err);
    i2s_driver_uninstall(kSpeakerI2sPort);
    return false;
  }

  i2s_zero_dma_buffer(kSpeakerI2sPort);
  return true;
}

bool install_mic_i2s(MicChannel channel) {
  const i2s_config_t config = {
      .mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX),
      .sample_rate = AppConfig::AUDIO_SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
      .channel_format = channel == MicChannel::Left ? I2S_CHANNEL_FMT_ONLY_LEFT
                                                    : I2S_CHANNEL_FMT_ONLY_RIGHT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = AppConfig::I2S_DMA_BUFFER_COUNT,
      .dma_buf_len = AppConfig::I2S_DMA_BUFFER_LENGTH,
      .use_apll = false,
      .tx_desc_auto_clear = false,
      .fixed_mclk = 0,
  };

  const i2s_pin_config_t pin_config = {
      .bck_io_num = AppConfig::MIC_SCK_PIN,
      .ws_io_num = AppConfig::MIC_WS_PIN,
      .data_out_num = I2S_PIN_NO_CHANGE,
      .data_in_num = AppConfig::MIC_SD_PIN,
  };

  esp_err_t err = i2s_driver_install(kMicI2sPort, &config, 0, nullptr);
  if (err != ESP_OK) {
    Serial.printf("Mic I2S install failed: %d\n", err);
    return false;
  }

  err = i2s_set_pin(kMicI2sPort, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("Mic I2S pin setup failed: %d\n", err);
    i2s_driver_uninstall(kMicI2sPort);
    return false;
  }

  i2s_zero_dma_buffer(kMicI2sPort);
  return true;
}

int16_t attenuate_sample(int16_t sample) {
  int32_t scaled = static_cast<int32_t>(sample) * g_output_volume_percent;
  scaled /= 100;
  if (scaled > INT16_MAX) {
    scaled = INT16_MAX;
  } else if (scaled < INT16_MIN) {
    scaled = INT16_MIN;
  }
  return static_cast<int16_t>(scaled);
}

void write_wav_header(uint8_t* header, uint32_t pcm_size, uint32_t sample_rate,
                      uint16_t channels, uint16_t bits_per_sample) {
  const uint32_t byte_rate = sample_rate * channels * bits_per_sample / 8;
  const uint16_t block_align = channels * bits_per_sample / 8;
  const uint32_t riff_chunk_size = pcm_size + 36;

  memcpy(header + 0, "RIFF", 4);
  memcpy(header + 8, "WAVE", 4);
  memcpy(header + 12, "fmt ", 4);
  memcpy(header + 36, "data", 4);

  header[4] = static_cast<uint8_t>(riff_chunk_size & 0xFF);
  header[5] = static_cast<uint8_t>((riff_chunk_size >> 8) & 0xFF);
  header[6] = static_cast<uint8_t>((riff_chunk_size >> 16) & 0xFF);
  header[7] = static_cast<uint8_t>((riff_chunk_size >> 24) & 0xFF);
  header[16] = 16;
  header[20] = 1;
  header[22] = static_cast<uint8_t>(channels);
  header[24] = static_cast<uint8_t>(sample_rate & 0xFF);
  header[25] = static_cast<uint8_t>((sample_rate >> 8) & 0xFF);
  header[26] = static_cast<uint8_t>((sample_rate >> 16) & 0xFF);
  header[27] = static_cast<uint8_t>((sample_rate >> 24) & 0xFF);
  header[28] = static_cast<uint8_t>(byte_rate & 0xFF);
  header[29] = static_cast<uint8_t>((byte_rate >> 8) & 0xFF);
  header[30] = static_cast<uint8_t>((byte_rate >> 16) & 0xFF);
  header[31] = static_cast<uint8_t>((byte_rate >> 24) & 0xFF);
  header[32] = static_cast<uint8_t>(block_align);
  header[34] = static_cast<uint8_t>(bits_per_sample);
  header[40] = static_cast<uint8_t>(pcm_size & 0xFF);
  header[41] = static_cast<uint8_t>((pcm_size >> 8) & 0xFF);
  header[42] = static_cast<uint8_t>((pcm_size >> 16) & 0xFF);
  header[43] = static_cast<uint8_t>((pcm_size >> 24) & 0xFF);
}

int16_t convert_mic_sample(int32_t sample_32, int32_t& dc_estimate) {
  sample_32 >>= kMicInputShift;
  dc_estimate += (sample_32 - dc_estimate) / 64;
  int32_t filtered = sample_32 - dc_estimate;

  if (filtered > INT16_MAX) {
    filtered = INT16_MAX;
  } else if (filtered < INT16_MIN) {
    filtered = INT16_MIN;
  }
  return static_cast<int16_t>(filtered);
}

bool parse_wav(const std::vector<uint8_t>& wav_data, uint32_t& sample_rate,
               uint16_t& channels, uint16_t& bits_per_sample,
               const uint8_t*& pcm_data, size_t& pcm_size) {
  if (wav_data.size() < kWavHeaderSize) {
    Serial.println("WAV parse failed: file too small");
    return false;
  }

  if (memcmp(wav_data.data(), "RIFF", 4) != 0 ||
      memcmp(wav_data.data() + 8, "WAVE", 4) != 0) {
    Serial.println("WAV parse failed: invalid RIFF/WAVE header");
    return false;
  }

  sample_rate = static_cast<uint32_t>(wav_data[24]) |
                (static_cast<uint32_t>(wav_data[25]) << 8) |
                (static_cast<uint32_t>(wav_data[26]) << 16) |
                (static_cast<uint32_t>(wav_data[27]) << 24);
  channels = static_cast<uint16_t>(wav_data[22]) |
             (static_cast<uint16_t>(wav_data[23]) << 8);
  bits_per_sample = static_cast<uint16_t>(wav_data[34]) |
                    (static_cast<uint16_t>(wav_data[35]) << 8);

  size_t offset = 12;
  while (offset + 8 <= wav_data.size()) {
    const uint32_t chunk_size = static_cast<uint32_t>(wav_data[offset + 4]) |
                                (static_cast<uint32_t>(wav_data[offset + 5]) << 8) |
                                (static_cast<uint32_t>(wav_data[offset + 6]) << 16) |
                                (static_cast<uint32_t>(wav_data[offset + 7]) << 24);
    if (memcmp(wav_data.data() + offset, "data", 4) == 0) {
      const size_t data_offset = offset + 8;
      if (data_offset + chunk_size > wav_data.size()) {
        Serial.println("WAV parse failed: truncated data chunk");
        return false;
      }
      pcm_data = wav_data.data() + data_offset;
      pcm_size = chunk_size;
      return true;
    }
    offset += 8 + chunk_size;
  }

  Serial.println("WAV parse failed: no data chunk");
  return false;
}

}  // namespace

bool audio_init() {
  static bool initialized = false;
  if (initialized) {
    return true;
  }

  const bool mic_ok = install_mic_i2s(g_mic_channel);
  const bool spk_ok = install_speaker_i2s();
  initialized = mic_ok && spk_ok;
  return initialized;
}

bool audio_set_mic_channel(MicChannel channel) {
  if (channel == g_mic_channel) {
    return true;
  }

  i2s_driver_uninstall(kMicI2sPort);
  if (!install_mic_i2s(channel)) {
    return false;
  }

  g_mic_channel = channel;
  Serial.printf("Mic channel set to: %s\n",
                channel == MicChannel::Left ? "left" : "right");
  return true;
}

void audio_set_output_volume(uint8_t percent) {
  g_output_volume_percent = std::min<uint8_t>(percent, 100);
  Serial.printf("Output volume set to: %u%%\n",
                static_cast<unsigned>(g_output_volume_percent));
}

uint8_t audio_get_output_volume() { return g_output_volume_percent; }

std::vector<uint8_t> audio_record(uint32_t duration_ms) {
  std::vector<uint8_t> wav_data;
  if (duration_ms == 0) {
    return wav_data;
  }

  const size_t total_samples =
      (AppConfig::AUDIO_SAMPLE_RATE * duration_ms) / 1000U;
  const size_t pcm_bytes = total_samples * sizeof(int16_t);
  wav_data.resize(kWavHeaderSize + pcm_bytes);
  write_wav_header(wav_data.data(), pcm_bytes, AppConfig::AUDIO_SAMPLE_RATE,
                   AppConfig::AUDIO_CHANNELS, AppConfig::AUDIO_BITS_PER_SAMPLE);

  std::vector<int32_t> raw_buffer(AppConfig::I2S_DMA_BUFFER_LENGTH);
  auto* pcm_out =
      reinterpret_cast<int16_t*>(wav_data.data() + kWavHeaderSize);
  size_t captured_samples = 0;
  int32_t dc_estimate = 0;

  i2s_zero_dma_buffer(kMicI2sPort);
  while (captured_samples < total_samples) {
    size_t bytes_read = 0;
    const esp_err_t err = i2s_read(kMicI2sPort, raw_buffer.data(),
                                   raw_buffer.size() * sizeof(int32_t),
                                   &bytes_read, pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
      Serial.printf("I2S mic read failed: %d\n", err);
      break;
    }

    const size_t samples_read = bytes_read / sizeof(int32_t);
    for (size_t i = 0; i < samples_read && captured_samples < total_samples; ++i) {
      pcm_out[captured_samples++] = convert_mic_sample(raw_buffer[i], dc_estimate);
    }
  }

  const size_t final_pcm_bytes = captured_samples * sizeof(int16_t);
  wav_data.resize(kWavHeaderSize + final_pcm_bytes);
  write_wav_header(wav_data.data(), final_pcm_bytes, AppConfig::AUDIO_SAMPLE_RATE,
                   AppConfig::AUDIO_CHANNELS, AppConfig::AUDIO_BITS_PER_SAMPLE);

  Serial.printf("Recorded %u samples (%u bytes WAV)\n",
                static_cast<unsigned>(captured_samples),
                static_cast<unsigned>(wav_data.size()));
  return wav_data;
}

bool audio_play_wav(const std::vector<uint8_t>& wav_data) {
  if (wav_data.empty()) {
    Serial.println("Playback skipped: empty WAV data");
    return false;
  }

  uint32_t sample_rate = 0;
  uint16_t channels = 0;
  uint16_t bits_per_sample = 0;
  const uint8_t* pcm_data = nullptr;
  size_t pcm_size = 0;

  if (!parse_wav(wav_data, sample_rate, channels, bits_per_sample, pcm_data, pcm_size)) {
    return false;
  }

  if (channels != 1 || bits_per_sample != 16) {
    Serial.printf("Playback rejected: unsupported WAV format channels=%u bits=%u\n",
                  channels, bits_per_sample);
    return false;
  }

  return audio_play_pcm_16_mono(reinterpret_cast<const int16_t*>(pcm_data),
                                pcm_size / sizeof(int16_t), sample_rate);
}

bool audio_play_pcm_16_mono(const int16_t* pcm_data, size_t sample_count,
                            uint32_t sample_rate) {
  if (!audio_begin_stream(sample_rate)) {
    return false;
  }
  if (!audio_write_pcm_16_mono(pcm_data, sample_count)) {
    audio_end_stream();
    return false;
  }
  audio_end_stream();
  return true;
}

bool audio_play_tone(uint16_t frequency_hz, uint16_t duration_ms) {
  if (frequency_hz == 0 || duration_ms == 0) {
    Serial.println("Tone playback rejected: invalid arguments");
    return false;
  }

  const size_t sample_count =
      (static_cast<size_t>(AppConfig::AUDIO_SAMPLE_RATE) * duration_ms) / 1000U;
  const size_t fade_samples =
      std::min(sample_count / 4, static_cast<size_t>(AppConfig::AUDIO_SAMPLE_RATE / 100));
  std::vector<int16_t> mono_buffer(sample_count);

  for (size_t index = 0; index < sample_count; ++index) {
    float envelope = 1.0f;
    if (index < fade_samples) {
      envelope = static_cast<float>(index) /
                 static_cast<float>(std::max<size_t>(1, fade_samples));
    } else if (index + fade_samples >= sample_count) {
      envelope = static_cast<float>(sample_count - index - 1) /
                 static_cast<float>(std::max<size_t>(1, fade_samples));
    }

    const float phase =
        2.0f * kPi * static_cast<float>(frequency_hz) * static_cast<float>(index) /
        static_cast<float>(AppConfig::AUDIO_SAMPLE_RATE);
    mono_buffer[index] =
        static_cast<int16_t>(std::sin(phase) * envelope * 18000.0f);
  }

  if (!audio_play_pcm_16_mono(mono_buffer.data(), mono_buffer.size(),
                              AppConfig::AUDIO_SAMPLE_RATE)) {
    Serial.println("Tone playback failed");
    return false;
  }

  Serial.println("Tone playback complete");
  return true;
}

bool audio_begin_stream(uint32_t sample_rate) {
  if (sample_rate == 0) {
    Serial.println("PCM stream rejected: invalid sample rate");
    return false;
  }

  if (i2s_set_clk(kSpeakerI2sPort, sample_rate, I2S_BITS_PER_SAMPLE_16BIT,
                  I2S_CHANNEL_STEREO) != ESP_OK) {
    Serial.println("PCM stream failed: unable to set I2S clock");
    return false;
  }

  g_stream_active = true;
  g_stream_sample_rate = sample_rate;
  Serial.printf("Playback started: %u Hz stream\n",
                static_cast<unsigned>(sample_rate));
  return true;
}

bool audio_write_pcm_16_mono(const int16_t* pcm_data, size_t sample_count) {
  if (!g_stream_active || pcm_data == nullptr || sample_count == 0) {
    Serial.println("PCM stream write rejected");
    return false;
  }

  std::vector<int16_t> stereo_buffer(512 * 2);
  size_t written_samples = 0;
  while (written_samples < sample_count) {
    const size_t mono_chunk_samples =
        std::min(static_cast<size_t>(512), sample_count - written_samples);
    for (size_t i = 0; i < mono_chunk_samples; ++i) {
      const int16_t sample = attenuate_sample(pcm_data[written_samples + i]);
      stereo_buffer[i * 2] = sample;
      stereo_buffer[i * 2 + 1] = sample;
    }

    size_t bytes_written = 0;
    const size_t chunk_size = mono_chunk_samples * 2 * sizeof(int16_t);
    const esp_err_t err =
        i2s_write(kSpeakerI2sPort, stereo_buffer.data(), chunk_size, &bytes_written,
                  portMAX_DELAY);
    if (err != ESP_OK) {
      Serial.printf("I2S speaker write failed: %d\n", err);
      return false;
    }

    written_samples += bytes_written / (2 * sizeof(int16_t));
  }

  return true;
}

bool audio_write_pcm_16_mono_bytes(const uint8_t* pcm_bytes, size_t byte_count) {
  if ((byte_count % sizeof(int16_t)) != 0) {
    Serial.println("PCM byte stream rejected: odd byte count");
    return false;
  }

  return audio_write_pcm_16_mono(reinterpret_cast<const int16_t*>(pcm_bytes),
                                 byte_count / sizeof(int16_t));
}

void audio_end_stream() {
  if (!g_stream_active) {
    return;
  }

  i2s_zero_dma_buffer(kSpeakerI2sPort);
  Serial.printf("Playback complete: %u Hz stream\n",
                static_cast<unsigned>(g_stream_sample_rate));
  g_stream_active = false;
  g_stream_sample_rate = 0;
}
