#pragma once

#include <Arduino.h>
#include <vector>

bool asr_is_configured();
bool asr_transcribe_audio_bytes(const String& mime_type,
                                const std::vector<uint8_t>& audio_bytes,
                                String& transcript,
                                String* error_message = nullptr);
