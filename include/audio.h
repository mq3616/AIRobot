#pragma once

#include <Arduino.h>
#include <vector>

enum class MicChannel {
  Left,
  Right,
};

bool audio_init();
bool audio_begin_stream(uint32_t sample_rate);
bool audio_write_pcm_16_mono(const int16_t* pcm_data, size_t sample_count);
bool audio_write_pcm_16_mono_bytes(const uint8_t* pcm_bytes, size_t byte_count);
void audio_end_stream();
bool audio_play_pcm_16_mono(const int16_t* pcm_data, size_t sample_count,
                            uint32_t sample_rate);
bool audio_play_wav(const std::vector<uint8_t>& wav_data);
bool audio_play_tone(uint16_t frequency_hz, uint16_t duration_ms);
void audio_set_output_volume(uint8_t percent);
uint8_t audio_get_output_volume();
bool audio_set_mic_channel(MicChannel channel);
std::vector<uint8_t> audio_record(uint32_t duration_ms);
