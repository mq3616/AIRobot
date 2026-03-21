#pragma once

#include <Arduino.h>

bool chat_is_configured();
bool chat_complete_once(const String& system_prompt, const String& user_text,
                        String& assistant_reply, String* error_message = nullptr);
