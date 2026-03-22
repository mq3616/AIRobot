#pragma once

#include <Arduino.h>
#include <vector>

struct ChatMessage {
  String role;
  String content;
};

bool chat_is_configured();
bool chat_complete_once(const String& system_prompt, const String& user_text,
                        String& assistant_reply, String* error_message = nullptr);
bool chat_complete_with_messages(const String& system_prompt,
                                 const std::vector<ChatMessage>& messages,
                                 String& assistant_reply,
                                 String* error_message = nullptr);
