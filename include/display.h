#pragma once

#include <Arduino.h>

enum class DisplayStatus {
  Booting,
  Ready,
  WifiConnecting,
  WifiConnected,
  WifiDisconnected,
  Recording,
  Playing,
  Error,
};

enum class FaceEmotion {
  Angry,
  Cry,
  Smile,
  Laugh,
  Fear,
  Surprised,
  Tired,
  Curious,
  Wink,
};

bool display_init();
void display_set_status(DisplayStatus status, const String& detail = "");
void display_set_emotion(FaceEmotion emotion, const String& detail = "");
void display_set_wifi_connected(bool connected);
void display_set_ip(const String& ip);
bool display_take_emotion_change(FaceEmotion& emotion);
void display_update();
