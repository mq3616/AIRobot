#pragma once

#include <Arduino.h>

enum class MelodyInstrument {
  Piano,
  MusicBox,
  Guitar,
};

enum class MelodySong {
  Canon,
  ToriNoUta,
  TheTruthThatYouLeave,
  MySoul,
  QiFengLe,
  ChuanYueShiKongDeSiNian,
  CastleInTheSky,
};

using MelodyServiceCallback = void (*)();

const char* melody_instrument_name(MelodyInstrument instrument);
const char* melody_song_name(MelodySong song);
bool melody_parse_instrument(const String& value, MelodyInstrument& instrument);
bool melody_parse_song(const String& value, MelodySong& song);
void melody_set_service_callback(MelodyServiceCallback callback);
void melody_request_stop();
void melody_clear_stop_request();
bool melody_stop_requested();
void melody_set_musicbox_accompaniment_enabled(bool enabled);
bool melody_musicbox_accompaniment_enabled();
bool melody_play_song(MelodyInstrument instrument, MelodySong song);
bool melody_play_random();
