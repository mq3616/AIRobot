#pragma once

#include <Arduino.h>

bool tts_is_configured();
bool tts_stream_text(const String& text, String* error_message = nullptr);
