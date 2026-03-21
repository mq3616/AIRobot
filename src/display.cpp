#include "display.h"

#include <Wire.h>

#include <Adafruit_SSD1306.h>
#include <FluxGarage_RoboEyes.h>

#include "app_config.h"

namespace {

TwoWire g_oled_wire = TwoWire(0);
Adafruit_SSD1306 g_display(AppConfig::OLED_WIDTH, AppConfig::OLED_HEIGHT, &g_oled_wire, -1);
RoboEyes<Adafruit_SSD1306> g_robo_eyes(g_display);

bool g_initialized = false;
bool g_wifi_connected = false;
String g_ip_address;
DisplayStatus g_status = DisplayStatus::Booting;
FaceEmotion g_emotion = FaceEmotion::Smile;
String g_detail;
uint32_t g_last_update_ms = 0;
uint32_t g_next_emotion_ms = 0;
size_t g_emotion_index = 0;
bool g_emotion_changed = false;
bool g_curious_left = false;

constexpr FaceEmotion kEmotionCycle[] = {
    FaceEmotion::Smile,
    FaceEmotion::Laugh,
    FaceEmotion::Curious,
    FaceEmotion::Fear,
    FaceEmotion::Surprised,
    FaceEmotion::Cry,
    FaceEmotion::Tired,
    FaceEmotion::Angry,
    FaceEmotion::Wink,
};

void schedule_next_emotion() {
  g_next_emotion_ms = millis() + 7000;
}

FaceEmotion emotion_for_status(DisplayStatus status) {
  switch (status) {
    case DisplayStatus::Booting:
      return FaceEmotion::Fear;
    case DisplayStatus::Ready:
      return FaceEmotion::Smile;
    case DisplayStatus::WifiConnecting:
      return FaceEmotion::Curious;
    case DisplayStatus::WifiConnected:
      return FaceEmotion::Smile;
    case DisplayStatus::WifiDisconnected:
      return FaceEmotion::Cry;
    case DisplayStatus::Recording:
      return FaceEmotion::Curious;
    case DisplayStatus::Playing:
      return FaceEmotion::Laugh;
    case DisplayStatus::Error:
      return FaceEmotion::Angry;
  }
  return FaceEmotion::Smile;
}

void print_i2c_scan() {
  Serial.println("I2C scan start");
  bool found = false;
  for (uint8_t address = 1; address < 127; ++address) {
    g_oled_wire.beginTransmission(address);
    if (g_oled_wire.endTransmission() == 0) {
      Serial.printf("I2C device found at 0x%02X\n", address);
      found = true;
    }
  }
  if (!found) {
    Serial.println("I2C scan: no device found");
  }
}

void configure_default_face() {
  g_robo_eyes.setDisplayColors(0, 1);
  g_robo_eyes.setWidth(34, 34);
  g_robo_eyes.setHeight(34, 34);
  g_robo_eyes.setBorderradius(8, 8);
  g_robo_eyes.setSpacebetween(14);
  g_robo_eyes.setAutoblinker(ON, 2, 2);
  g_robo_eyes.setIdleMode(ON, 2, 2);
  g_robo_eyes.setCuriosity(OFF);
  g_robo_eyes.setCyclops(OFF);
  g_robo_eyes.setSweat(OFF);
  g_robo_eyes.setHFlicker(OFF, 0);
  g_robo_eyes.setVFlicker(OFF, 0);
  g_robo_eyes.setMood(DEFAULT);
  g_robo_eyes.setPosition(DEFAULT);
  g_robo_eyes.open();
}

void apply_emotion_to_face(bool trigger_animation) {
  configure_default_face();

  switch (g_emotion) {
    case FaceEmotion::Angry:
      g_robo_eyes.setMood(ANGRY);
      g_robo_eyes.setHFlicker(ON, 1);
      break;
    case FaceEmotion::Cry:
      g_robo_eyes.setMood(TIRED);
      g_robo_eyes.setSweat(ON);
      g_robo_eyes.setPosition(S);
      break;
    case FaceEmotion::Smile:
      g_robo_eyes.setMood(HAPPY);
      break;
    case FaceEmotion::Laugh:
      g_robo_eyes.setMood(HAPPY);
      if (trigger_animation) {
        g_robo_eyes.anim_laugh();
      }
      break;
    case FaceEmotion::Fear:
      g_robo_eyes.setCuriosity(ON);
      g_robo_eyes.setSweat(ON);
      g_robo_eyes.setPosition(N);
      break;
    case FaceEmotion::Surprised:
      g_robo_eyes.setCuriosity(ON);
      g_robo_eyes.setHeight(30, 30);
      g_robo_eyes.setBorderradius(10, 10);
      g_robo_eyes.setPosition(N);
      break;
    case FaceEmotion::Tired:
      g_robo_eyes.setMood(TIRED);
      g_robo_eyes.setIdleMode(OFF);
      g_robo_eyes.setPosition(S);
      break;
    case FaceEmotion::Curious:
      g_robo_eyes.setCuriosity(ON);
      g_robo_eyes.setIdleMode(OFF);
      g_robo_eyes.setPosition(g_curious_left ? W : E);
      g_curious_left = !g_curious_left;
      break;
    case FaceEmotion::Wink:
      g_robo_eyes.setMood(HAPPY);
      g_robo_eyes.setIdleMode(OFF);
      if (trigger_animation) {
        g_robo_eyes.blink(1, 0);
      }
      break;
  }
}

void set_emotion_internal(FaceEmotion emotion, const String& detail, bool reschedule_cycle) {
  const bool changed = emotion != g_emotion;
  g_emotion = emotion;
  g_detail = detail;
  if (reschedule_cycle) {
    schedule_next_emotion();
  }
  if (changed) {
    g_emotion_changed = true;
    apply_emotion_to_face(true);
  } else {
    apply_emotion_to_face(false);
  }
}

}  // namespace

bool display_init() {
  if (g_initialized) {
    return true;
  }

  g_oled_wire.begin(AppConfig::OLED_SDA_PIN, AppConfig::OLED_SCL_PIN);
  print_i2c_scan();

  if (!g_display.begin(SSD1306_SWITCHCAPVCC, AppConfig::OLED_I2C_ADDRESS) &&
      !g_display.begin(SSD1306_SWITCHCAPVCC, AppConfig::OLED_I2C_FALLBACK_ADDRESS)) {
    Serial.println("OLED init failed at 0x3C and 0x3D");
    return false;
  }

  randomSeed(micros());
  g_robo_eyes.begin(AppConfig::OLED_WIDTH, AppConfig::OLED_HEIGHT, 30);
  g_initialized = true;
  g_status = DisplayStatus::Booting;
  g_emotion = FaceEmotion::Fear;
  g_emotion_changed = true;
  apply_emotion_to_face(true);
  schedule_next_emotion();
  return true;
}

void display_set_status(DisplayStatus status, const String& detail) {
  g_status = status;
  set_emotion_internal(emotion_for_status(status), detail, false);
}

void display_set_emotion(FaceEmotion emotion, const String& detail) {
  set_emotion_internal(emotion, detail, true);
}

void display_set_wifi_connected(bool connected) {
  g_wifi_connected = connected;
}

void display_set_ip(const String& ip) {
  g_ip_address = ip;
}

bool display_take_emotion_change(FaceEmotion& emotion) {
  if (!g_emotion_changed) {
    return false;
  }

  g_emotion_changed = false;
  emotion = g_emotion;
  return true;
}

void display_update() {
  if (!g_initialized) {
    return;
  }

  const uint32_t now = millis();
  if (now >= g_next_emotion_ms) {
    g_emotion_index = (g_emotion_index + 1) % (sizeof(kEmotionCycle) / sizeof(kEmotionCycle[0]));
    set_emotion_internal(kEmotionCycle[g_emotion_index], g_detail, true);
  }

  if ((now - g_last_update_ms) < 15) {
    return;
  }
  g_last_update_ms = now;
  g_robo_eyes.update();
}
