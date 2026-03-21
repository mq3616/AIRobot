#include <Arduino.h>

#include <algorithm>
#include <vector>

#include "app_config.h"
#include "app_wifi.h"
#include "audio.h"
#include "chat.h"
#include "device_profile.h"
#include "display.h"
#include "melody.h"
#include "tts.h"

namespace {

enum class SerialTransferMode {
  Idle,
  ReceivePlaybackWav,
};

enum class OnboardingState {
  Idle,
  WaitingForUserName,
  WaitingForRobotName,
};

String g_serial_buffer;
SerialTransferMode g_transfer_mode = SerialTransferMode::Idle;
OnboardingState g_onboarding_state = OnboardingState::Idle;
size_t g_expected_upload_size = 0;
std::vector<uint8_t> g_uploaded_wav;
uint32_t g_last_wifi_attempt_ms = 0;
String g_pending_user_name;
bool g_melody_loop_enabled = false;
bool g_melody_playing = false;
bool g_melody_manual_selection_pending = false;
MelodySong g_loop_song = MelodySong::Canon;
MelodyInstrument g_loop_instrument = MelodyInstrument::MusicBox;

constexpr MelodySong kLoopPlaylist[] = {
    MelodySong::Canon,
    MelodySong::ToriNoUta,
    MelodySong::TheTruthThatYouLeave,
    MelodySong::MySoul,
    MelodySong::QiFengLe,
    MelodySong::ChuanYueShiKongDeSiNian,
};

size_t playlist_index_for_song(MelodySong song) {
  for (size_t i = 0; i < (sizeof(kLoopPlaylist) / sizeof(kLoopPlaylist[0])); ++i) {
    if (kLoopPlaylist[i] == song) {
      return i;
    }
  }
  return 0;
}

MelodySong next_playlist_song(MelodySong current) {
  const size_t current_index = playlist_index_for_song(current);
  const size_t next_index =
      (current_index + 1) % (sizeof(kLoopPlaylist) / sizeof(kLoopPlaylist[0]));
  return kLoopPlaylist[next_index];
}

bool take_serial_line(String& line) {
  while (Serial.available()) {
    const char ch = static_cast<char>(Serial.read());
    if (ch == '\r') {
      continue;
    }

    if (ch == '\n') {
      line = g_serial_buffer;
      g_serial_buffer = "";
      line.trim();
      return !line.isEmpty();
    }

    g_serial_buffer += ch;
  }

  return false;
}

int hex_value(char ch) {
  if (ch >= '0' && ch <= '9') {
    return ch - '0';
  }
  if (ch >= 'A' && ch <= 'F') {
    return 10 + (ch - 'A');
  }
  if (ch >= 'a' && ch <= 'f') {
    return 10 + (ch - 'a');
  }
  return -1;
}

bool decode_hex_line(const String& line, std::vector<uint8_t>& out) {
  if ((line.length() % 2) != 0) {
    return false;
  }

  for (unsigned i = 0; i < line.length(); i += 2) {
    const int hi = hex_value(line[i]);
    const int lo = hex_value(line[i + 1]);
    if (hi < 0 || lo < 0) {
      return false;
    }
    out.push_back(static_cast<uint8_t>((hi << 4) | lo));
  }

  return true;
}

void reset_upload() {
  g_transfer_mode = SerialTransferMode::Idle;
  g_expected_upload_size = 0;
  g_uploaded_wav.clear();
}

void print_hex_dump(const std::vector<uint8_t>& data) {
  static constexpr char kHexChars[] = "0123456789ABCDEF";
  static constexpr size_t kBytesPerLine = 32;

  for (size_t offset = 0; offset < data.size(); offset += kBytesPerLine) {
    const size_t chunk_size = std::min(kBytesPerLine, data.size() - offset);
    char line[kBytesPerLine * 2 + 1];
    for (size_t i = 0; i < chunk_size; ++i) {
      const uint8_t value = data[offset + i];
      line[i * 2] = kHexChars[(value >> 4) & 0x0F];
      line[i * 2 + 1] = kHexChars[value & 0x0F];
    }
    line[chunk_size * 2] = '\0';
    Serial.println(line);
  }
}

void ensure_wifi() {
  if (wifi_is_connected()) {
    return;
  }

  const uint32_t now = millis();
  if ((now - g_last_wifi_attempt_ms) < AppConfig::WIFI_RETRY_INTERVAL_MS) {
    return;
  }

  g_last_wifi_attempt_ms = now;
  wifi_connect();
}

void dump_recording(MicChannel channel) {
  display_set_status(DisplayStatus::Recording,
                     channel == MicChannel::Left ? "left mic" : "right mic");
  if (!audio_set_mic_channel(channel)) {
    Serial.println("Recording export failed: unable to switch microphone channel");
    display_set_status(DisplayStatus::Error, "mic switch failed");
    return;
  }

  Serial.printf("Recording export: recording %u ms...\n",
                static_cast<unsigned>(AppConfig::RECORD_DURATION_MS));
  std::vector<uint8_t> recorded_audio = audio_record(AppConfig::RECORD_DURATION_MS);
  if (recorded_audio.size() <= 44) {
    Serial.println("Recording export failed: no useful audio captured");
    display_set_status(DisplayStatus::Error, "recording empty");
    return;
  }

  Serial.println("BEGIN_WAV_HEX");
  Serial.printf("SIZE:%u\n", static_cast<unsigned>(recorded_audio.size()));
  print_hex_dump(recorded_audio);
  Serial.println("END_WAV_HEX");
  Serial.println("Recording export complete");
  display_set_status(DisplayStatus::Ready, "recording complete");
}

void print_help() {
  Serial.println("Commands:");
  Serial.println("  beep            -> play a short built-in test tone");
  Serial.println("  dl              -> record left mic channel and dump WAV over serial");
  Serial.println("  dr              -> record right mic channel and dump WAV over serial");
  Serial.println("  volume          -> print current playback volume");
  Serial.println("  volume <0-100>  -> set playback volume");
  Serial.println("  accomp          -> print musicbox accompaniment status");
  Serial.println("  accomp on       -> enable musicbox accompaniment");
  Serial.println("  accomp off      -> disable musicbox accompaniment");
  Serial.println("  melody canon musicbox");
  Serial.println("  melody tori musicbox");
  Serial.println("  melody truth musicbox");
  Serial.println("  melody juebieshu musicbox");
  Serial.println("  melody qifeng musicbox");
  Serial.println("  melody sinian musicbox");
  Serial.println("  melody stop");
  Serial.println("  emo angry       -> face emotion: angry");
  Serial.println("  emo cry         -> face emotion: cry");
  Serial.println("  emo smile       -> face emotion: smile");
  Serial.println("  emo laugh       -> face emotion: laugh");
  Serial.println("  emo fear        -> face emotion: fear");
  Serial.println("  emo surprised   -> face emotion: surprised");
  Serial.println("  emo tired       -> face emotion: tired");
  Serial.println("  emo curious     -> face emotion: curious");
  Serial.println("  emo wink        -> face emotion: wink");
  Serial.println("  profile         -> print saved user and robot names");
  Serial.println("  prompt          -> print current system prompt");
  Serial.println("  initbot         -> run first-time robot setup");
  Serial.println("  resetprofile    -> clear saved user and robot names");
  Serial.println("  mem             -> print heap, psram, and upload limits");
  Serial.println("  wifi            -> print WiFi status");
  Serial.println("  reconnect       -> retry WiFi connection now");
  Serial.println("  playhex <size>  -> receive WAV hex payload over serial and play it");
  Serial.println("  say <text>      -> call Bailian TTS and stream the reply");
  Serial.println("  ask <text>      -> ask Bailian chat, then speak the answer");
  Serial.println("  help            -> print this help");
}

void print_memory_status() {
  Serial.printf("Heap free: %u bytes, largest block: %u bytes\n",
                static_cast<unsigned>(ESP.getFreeHeap()),
                static_cast<unsigned>(ESP.getMaxAllocHeap()));
  if (psramFound()) {
    Serial.printf("PSRAM free: %u bytes, largest block: %u bytes\n",
                  static_cast<unsigned>(ESP.getFreePsram()),
                  static_cast<unsigned>(ESP.getMaxAllocPsram()));
  } else {
    Serial.println("PSRAM: not available");
  }

  Serial.printf("Serial WAV limit: %u bytes (recommended <= %u bytes)\n",
                static_cast<unsigned>(AppConfig::SERIAL_WAV_UPLOAD_LIMIT),
                static_cast<unsigned>(AppConfig::SERIAL_WAV_RECOMMENDED_LIMIT));
}

bool handle_volume_command(const String& line) {
  if (!line.startsWith("volume")) {
    return false;
  }

  if (line.equalsIgnoreCase("volume")) {
    Serial.printf("Current playback volume: %u%%\n",
                  static_cast<unsigned>(audio_get_output_volume()));
    return true;
  }

  if (!line.startsWith("volume ")) {
    Serial.println("Usage: volume <0-100>");
    return true;
  }

  String value_token = line.substring(7);
  value_token.trim();
  if (value_token.isEmpty()) {
    Serial.println("Usage: volume <0-100>");
    return true;
  }

  for (unsigned i = 0; i < value_token.length(); ++i) {
    if (!isDigit(value_token[i])) {
      Serial.println("Volume rejected: use an integer 0-100");
      return true;
    }
  }

  const long volume = value_token.toInt();
  if (volume < 0 || volume > 100) {
    Serial.println("Volume rejected: use an integer 0-100");
    return true;
  }

  audio_set_output_volume(static_cast<uint8_t>(volume));
  return true;
}

bool handle_accompaniment_command(const String& line) {
  if (!line.startsWith("accomp")) {
    return false;
  }

  if (line.equalsIgnoreCase("accomp")) {
    Serial.printf("Musicbox accompaniment: %s\n",
                  melody_musicbox_accompaniment_enabled() ? "on" : "off");
    return true;
  }

  if (line.equalsIgnoreCase("accomp on")) {
    melody_set_musicbox_accompaniment_enabled(true);
    Serial.println("Musicbox accompaniment enabled");
    return true;
  }

  if (line.equalsIgnoreCase("accomp off")) {
    melody_set_musicbox_accompaniment_enabled(false);
    Serial.println("Musicbox accompaniment disabled");
    return true;
  }

  Serial.println("Usage: accomp <on|off>");
  return true;
}

void print_profile() {
  const DeviceProfile& profile = device_profile_get();
  if (!profile.initialized) {
    Serial.println("Profile: not initialized");
    return;
  }

  Serial.printf("Profile: user=%s, robot=%s\n", profile.user_name.c_str(),
                profile.robot_name.c_str());
}

void begin_onboarding() {
  g_pending_user_name = "";
  g_onboarding_state = OnboardingState::WaitingForUserName;
  display_set_status(DisplayStatus::Ready, "need profile");
  Serial.println("First-time setup");
  Serial.println("Who are you? Type your name and press Enter.");
}

bool handle_onboarding_input(const String& line) {
  if (g_onboarding_state == OnboardingState::Idle) {
    return false;
  }

  if (g_onboarding_state == OnboardingState::WaitingForUserName) {
    g_pending_user_name = line;
    g_pending_user_name.trim();
    if (g_pending_user_name.isEmpty()) {
      Serial.println("Name cannot be empty. Please type your name.");
      return true;
    }

    g_onboarding_state = OnboardingState::WaitingForRobotName;
    Serial.println("What is my name? Type the robot name and press Enter.");
    return true;
  }

  String robot_name = line;
  robot_name.trim();
  if (robot_name.isEmpty()) {
    Serial.println("Robot name cannot be empty. Please type the robot name.");
    return true;
  }

  if (!device_profile_set_names(g_pending_user_name, robot_name)) {
    Serial.println("Profile save failed");
    display_set_status(DisplayStatus::Error, "profile save failed");
    g_onboarding_state = OnboardingState::Idle;
    g_pending_user_name = "";
    return true;
  }

  g_onboarding_state = OnboardingState::Idle;
  Serial.println("Profile saved");
  print_profile();
  Serial.println("Persona: goofy by default");
  Serial.println("System prompt:");
  Serial.println(device_profile_build_system_prompt());
  display_set_status(DisplayStatus::Ready, "profile saved");
  g_pending_user_name = "";
  return true;
}

bool handle_emotion_command(const String& line) {
  if (!line.startsWith("emo ")) {
    return false;
  }

  const String name = line.substring(4);
  if (name.equalsIgnoreCase("angry")) {
    display_set_emotion(FaceEmotion::Angry, "manual angry");
  } else if (name.equalsIgnoreCase("cry")) {
    display_set_emotion(FaceEmotion::Cry, "manual cry");
  } else if (name.equalsIgnoreCase("smile")) {
    display_set_emotion(FaceEmotion::Smile, "manual smile");
  } else if (name.equalsIgnoreCase("laugh")) {
    display_set_emotion(FaceEmotion::Laugh, "manual laugh");
  } else if (name.equalsIgnoreCase("fear")) {
    display_set_emotion(FaceEmotion::Fear, "manual fear");
  } else if (name.equalsIgnoreCase("surprised")) {
    display_set_emotion(FaceEmotion::Surprised, "manual surprised");
  } else if (name.equalsIgnoreCase("tired")) {
    display_set_emotion(FaceEmotion::Tired, "manual tired");
  } else if (name.equalsIgnoreCase("curious")) {
    display_set_emotion(FaceEmotion::Curious, "manual curious");
  } else if (name.equalsIgnoreCase("wink")) {
    display_set_emotion(FaceEmotion::Wink, "manual wink");
  } else {
    Serial.println("Unknown emotion");
  }

  return true;
}

void print_wiring() {
  Serial.println("Wiring:");
  Serial.println("  ESP32-S3 3V3  -> MAX98357A VIN");
  Serial.println("  ESP32-S3 GND  -> MAX98357A GND");
  Serial.printf("  ESP32-S3 GPIO%d -> MAX98357A DIN\n", AppConfig::SPK_DIN_PIN);
  Serial.printf("  ESP32-S3 GPIO%d -> MAX98357A BCLK\n", AppConfig::SPK_BCLK_PIN);
  Serial.printf("  ESP32-S3 GPIO%d -> MAX98357A LRC\n", AppConfig::SPK_LRC_PIN);
  Serial.println("  MAX98357A +/- -> speaker +/-");
}

void begin_uploaded_playback(size_t payload_size) {
  if (payload_size == 0 || payload_size > AppConfig::SERIAL_WAV_UPLOAD_LIMIT) {
    Serial.println("Upload rejected: invalid payload size");
    return;
  }

  if (payload_size > AppConfig::SERIAL_WAV_RECOMMENDED_LIMIT) {
    Serial.printf("Upload warning: %u bytes exceeds recommended %u bytes\n",
                  static_cast<unsigned>(payload_size),
                  static_cast<unsigned>(AppConfig::SERIAL_WAV_RECOMMENDED_LIMIT));
  }

  reset_upload();
  g_transfer_mode = SerialTransferMode::ReceivePlaybackWav;
  g_expected_upload_size = payload_size;
  g_uploaded_wav.reserve(payload_size);
  Serial.printf("Ready to receive WAV over serial: %u bytes\n",
                static_cast<unsigned>(payload_size));
}

bool handle_transfer_line(const String& line) {
  if (g_transfer_mode == SerialTransferMode::Idle) {
    return false;
  }

  if (line.equalsIgnoreCase("ENDHEX")) {
    if (g_uploaded_wav.size() != g_expected_upload_size) {
      Serial.printf("Upload size mismatch: expected %u, got %u\n",
                    static_cast<unsigned>(g_expected_upload_size),
                    static_cast<unsigned>(g_uploaded_wav.size()));
      display_set_status(DisplayStatus::Error, "upload mismatch");
      reset_upload();
      return true;
    }

    display_set_status(DisplayStatus::Playing, "wav playback");
    audio_play_wav(g_uploaded_wav);
    display_set_status(DisplayStatus::Ready, "playback complete");
    reset_upload();
    return true;
  }

  if (!decode_hex_line(line, g_uploaded_wav)) {
    Serial.println("Upload failed: invalid hex payload");
    display_set_status(DisplayStatus::Error, "invalid hex");
    reset_upload();
    return true;
  }

  if (g_uploaded_wav.size() > g_expected_upload_size) {
    Serial.println("Upload failed: payload larger than declared size");
    display_set_status(DisplayStatus::Error, "payload too large");
    reset_upload();
    return true;
  }

  return true;
}

bool handle_melody_command(const String& line) {
  if (!line.startsWith("melody")) {
    return false;
  }

  if (line.equalsIgnoreCase("melody stop")) {
    g_melody_loop_enabled = false;
    g_melody_manual_selection_pending = false;
    if (g_melody_playing) {
      melody_request_stop();
      Serial.println("Melody stop requested");
    }
    display_set_status(DisplayStatus::Ready, "melody stopped");
    Serial.println("Melody loop stopped");
    return true;
  }

  const int first_space = line.indexOf(' ');
  if (first_space < 0) {
    Serial.println(
        "Usage: melody <canon|tori|truth|juebieshu|qifeng|sinian> "
        "<musicbox|piano> | melody stop");
    return true;
  }

  const int second_space = line.indexOf(' ', first_space + 1);
  if (second_space < 0) {
    Serial.println(
        "Usage: melody <canon|tori|truth|juebieshu|qifeng|sinian> "
        "<musicbox|piano> | melody stop");
    return true;
  }

  String song_token = line.substring(first_space + 1, second_space);
  String instrument_token = line.substring(second_space + 1);
  song_token.trim();
  instrument_token.trim();

  MelodySong song;
  MelodyInstrument instrument;
  if (!melody_parse_song(song_token, song)) {
    Serial.println(
        "Unknown melody song. Use canon, tori, truth, juebieshu, qifeng, or "
        "sinian.");
    return true;
  }
  if (!melody_parse_instrument(instrument_token, instrument)) {
    Serial.println("Unknown melody instrument. Use musicbox or piano.");
    return true;
  }

  g_loop_song = song;
  g_loop_instrument = instrument;
  g_melody_loop_enabled = true;
  g_melody_manual_selection_pending = true;
  if (g_melody_playing) {
    melody_request_stop();
    Serial.printf("Melody switch requested: %s %s\n", melody_song_name(g_loop_song),
                  melody_instrument_name(g_loop_instrument));
  } else {
    display_set_status(DisplayStatus::Playing, "melody queued");
    Serial.printf("Melody queued: %s %s\n", melody_song_name(g_loop_song),
                  melody_instrument_name(g_loop_instrument));
  }
  return true;
}

void service_serial_during_melody_playback() {
  String line;
  if (!take_serial_line(line)) {
    return;
  }

  if (handle_volume_command(line) || handle_accompaniment_command(line) ||
      handle_melody_command(line)) {
    Serial.println("Ready");
  }
}

bool handle_tts_command(const String& line) {
  if (!line.startsWith("say ")) {
    return false;
  }

  String text = line.substring(4);
  text.trim();
  if (text.isEmpty()) {
    Serial.println("Usage: say <text>");
    return true;
  }

  if (!wifi_is_connected()) {
    Serial.println("TTS unavailable: WiFi disconnected");
    display_set_status(DisplayStatus::WifiDisconnected, "tts needs wifi");
    return true;
  }

  const bool melody_was_enabled = g_melody_loop_enabled;
  g_melody_loop_enabled = false;
  display_set_status(DisplayStatus::Playing, "tts stream");

  String error_message;
  const bool ok = tts_stream_text(text, &error_message);
  if (ok) {
    Serial.println("TTS playback complete");
    display_set_status(DisplayStatus::Ready, "tts done");
  } else {
    Serial.printf("TTS playback failed: %s\n", error_message.c_str());
    display_set_status(DisplayStatus::Error, "tts failed");
  }

  g_melody_loop_enabled = melody_was_enabled;
  return true;
}

bool handle_chat_command(const String& line) {
  if (!line.startsWith("ask ")) {
    return false;
  }

  String text = line.substring(4);
  text.trim();
  if (text.isEmpty()) {
    Serial.println("Usage: ask <text>");
    return true;
  }

  if (!wifi_is_connected()) {
    Serial.println("Chat unavailable: WiFi disconnected");
    display_set_status(DisplayStatus::WifiDisconnected, "chat needs wifi");
    return true;
  }

  display_set_status(DisplayStatus::Ready, "chat request");
  String reply_text;
  String error_message;
  if (!chat_complete_once(device_profile_build_system_prompt(), text, reply_text,
                          &error_message)) {
    Serial.printf("Chat failed: %s\n", error_message.c_str());
    display_set_status(DisplayStatus::Error, "chat failed");
    return true;
  }

  Serial.print("Robot: ");
  Serial.println(reply_text);

  const bool melody_was_enabled = g_melody_loop_enabled;
  g_melody_loop_enabled = false;
  display_set_status(DisplayStatus::Playing, "chat tts");
  const bool tts_ok = tts_stream_text(reply_text, &error_message);
  if (!tts_ok) {
    Serial.printf("TTS playback failed: %s\n", error_message.c_str());
    display_set_status(DisplayStatus::Error, "tts failed");
  } else {
    display_set_status(DisplayStatus::Ready, "chat complete");
  }
  g_melody_loop_enabled = melody_was_enabled;
  return true;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1000);
  randomSeed(micros());

  Serial.println();
  Serial.println("Audio bridge booting");
  display_init();
  display_set_status(DisplayStatus::Booting, "init");
  print_wiring();

  if (!audio_init()) {
    Serial.println("Audio initialization failed");
    display_set_status(DisplayStatus::Error, "audio init failed");
  } else {
    Serial.println("Audio initialization complete");
    display_set_status(DisplayStatus::Ready, "audio ready");
    g_loop_song = MelodySong::Canon;
    g_loop_instrument = MelodyInstrument::MusicBox;
    g_melody_loop_enabled = false;
    g_melody_manual_selection_pending = false;
  }

  melody_set_service_callback(service_serial_during_melody_playback);

  g_last_wifi_attempt_ms = millis() - AppConfig::WIFI_RETRY_INTERVAL_MS;
  ensure_wifi();
  if (!device_profile_init()) {
    Serial.println("Profile storage init failed");
  }

  print_help();
  if (!device_profile_get().initialized) {
    begin_onboarding();
  } else {
    print_profile();
    Serial.println("System prompt:");
    Serial.println(device_profile_build_system_prompt());
  }
  Serial.println("Ready");
}

void loop() {
  ensure_wifi();
  display_update();
  FaceEmotion ignored_emotion;
  display_take_emotion_change(ignored_emotion);

  if (g_melody_loop_enabled && g_transfer_mode == SerialTransferMode::Idle &&
      g_onboarding_state == OnboardingState::Idle) {
    display_set_status(DisplayStatus::Playing, "melody loop");
    const MelodySong current_song = g_loop_song;
    const MelodyInstrument current_instrument = g_loop_instrument;
    const bool was_manual_selection = g_melody_manual_selection_pending;
    g_melody_manual_selection_pending = false;
    g_melody_playing = true;
    melody_clear_stop_request();
    const bool ok = melody_play_song(current_instrument, current_song);
    const bool interrupted = melody_stop_requested();
    melody_clear_stop_request();
    g_melody_playing = false;

    if (!ok) {
      display_set_status(DisplayStatus::Error, "melody failed");
      g_melody_loop_enabled = false;
    } else if (interrupted) {
      display_set_status(DisplayStatus::Ready,
                         g_melody_loop_enabled ? "melody switched" : "melody stopped");
    } else {
      display_set_status(DisplayStatus::Ready, "loop next");
      if (was_manual_selection) {
        // Keep the user-selected song as the current loop anchor after its first play.
      }
      g_loop_song = next_playlist_song(current_song);
    }
  }

  String line;
  if (take_serial_line(line)) {
    if (handle_transfer_line(line)) {
      display_set_status(DisplayStatus::Ready, "upload complete");
      Serial.println("Ready");
    } else if (handle_onboarding_input(line)) {
      Serial.println("Ready");
    } else if (handle_volume_command(line)) {
      Serial.println("Ready");
    } else if (handle_accompaniment_command(line)) {
      Serial.println("Ready");
    } else if (handle_melody_command(line)) {
      Serial.println("Ready");
    } else if (handle_chat_command(line)) {
      Serial.println("Ready");
    } else if (handle_tts_command(line)) {
      Serial.println("Ready");
    } else if (line.equalsIgnoreCase("beep")) {
      display_set_status(DisplayStatus::Playing, "beep");
      audio_play_tone(880, 180);
      display_set_status(DisplayStatus::Ready, "beep done");
      Serial.println("Ready");
    } else if (line.equalsIgnoreCase("dl")) {
      dump_recording(MicChannel::Left);
      Serial.println("Ready");
    } else if (line.equalsIgnoreCase("dr")) {
      dump_recording(MicChannel::Right);
      Serial.println("Ready");
    } else if (line.equalsIgnoreCase("wifi")) {
      wifi_print_status();
      Serial.println("Ready");
    } else if (line.equalsIgnoreCase("mem")) {
      print_memory_status();
      Serial.println("Ready");
    } else if (line.equalsIgnoreCase("profile")) {
      print_profile();
      Serial.println("Ready");
    } else if (line.equalsIgnoreCase("prompt")) {
      Serial.println(device_profile_build_system_prompt());
      Serial.println("Ready");
    } else if (line.equalsIgnoreCase("initbot")) {
      begin_onboarding();
      Serial.println("Ready");
    } else if (line.equalsIgnoreCase("resetprofile")) {
      if (device_profile_clear()) {
        Serial.println("Profile cleared");
        begin_onboarding();
      } else {
        Serial.println("Profile clear failed");
      }
      Serial.println("Ready");
    } else if (handle_emotion_command(line)) {
      Serial.println("Ready");
    } else if (line.equalsIgnoreCase("reconnect")) {
      g_last_wifi_attempt_ms = 0;
      ensure_wifi();
      Serial.println("Ready");
    } else if (line.startsWith("playhex ")) {
      const size_t payload_size = static_cast<size_t>(line.substring(8).toInt());
      begin_uploaded_playback(payload_size);
      display_set_status(DisplayStatus::Playing, "receiving wav");
      Serial.println("Ready");
    } else if (line.equalsIgnoreCase("help")) {
      print_help();
      Serial.println("Ready");
    } else {
      Serial.println("Unknown command");
      print_help();
      Serial.println("Ready");
    }
  }

  delay(20);
}
