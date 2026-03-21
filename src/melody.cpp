#include "melody.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "app_config.h"
#include "audio.h"
#include "canon_midi_data.h"
#include "chuanyuesikongdesinian_midi_data.h"
#include "juebieshu_midi_data.h"
#include "musicbox_sample_data.h"
#include "piano_sample_data.h"
#include "qifengle_midi_data.h"
#include "truth_that_you_leave_midi_data.h"
#include "tori_no_uta_midi_data.h"

namespace {

constexpr uint32_t kSampleRate = AppConfig::AUDIO_SAMPLE_RATE;
constexpr size_t kStreamChunkSamples = 256;
constexpr float kPi = 3.14159265358979323846f;
MelodyServiceCallback g_service_callback = nullptr;
volatile bool g_stop_requested = false;
volatile bool g_musicbox_accompaniment_enabled = false;

enum class RenderInstrument {
  PianoSample,
  MusicBox,
};

struct ScheduledNote {
  uint32_t start_sample;
  uint32_t sustain_samples;
  uint32_t total_samples;
  uint8_t midi;
  uint8_t role;
  float gain;
};

struct Voice {
  uint32_t age_samples;
  uint32_t sustain_samples;
  uint32_t total_samples;
  float gain;
  RenderInstrument instrument;
  uint8_t midi;
  float sample_position;
  float sample_step;
  float last_output;
  float previous_sample;
  float body_state;
  uint16_t sample_length;
  const int8_t* sample_data;
};

struct GenericMidiEvent {
  uint32_t start_tick;
  uint32_t duration_tick;
  uint8_t midi;
  uint8_t velocity;
  uint8_t role;
};

struct MusicBoxMasterState {
  float hp_input = 0.0f;
  float hp_output = 0.0f;
  float body_lowpass = 0.0f;
  float presence_band = 0.0f;
  float air_lowpass = 0.0f;
};

const PianoSampleData::Sample& find_best_sample(uint8_t midi) {
  const PianoSampleData::Sample* best = &PianoSampleData::kSamples[0];
  uint8_t best_distance = 127;
  for (size_t i = 0; i < PianoSampleData::kSampleCount; ++i) {
    const auto& candidate = PianoSampleData::kSamples[i];
    const uint8_t distance = static_cast<uint8_t>(
        std::abs(static_cast<int>(candidate.midi) - static_cast<int>(midi)));
    if (distance < best_distance) {
      best = &candidate;
      best_distance = distance;
    }
  }
  return *best;
}

const MusicBoxSampleData::Sample& find_best_musicbox_sample(uint8_t midi) {
  const MusicBoxSampleData::Sample* best = &MusicBoxSampleData::kSamples[0];
  uint8_t best_distance = 127;
  for (size_t i = 0; i < MusicBoxSampleData::kSampleCount; ++i) {
    const auto& candidate = MusicBoxSampleData::kSamples[i];
    const uint8_t distance = static_cast<uint8_t>(
        std::abs(static_cast<int>(candidate.midi) - static_cast<int>(midi)));
    if (distance < best_distance) {
      best = &candidate;
      best_distance = distance;
    }
  }
  return *best;
}

float sample_voice_value(const Voice& voice) {
  if (voice.sample_length < 2 ||
      voice.sample_position >= static_cast<float>(voice.sample_length - 1)) {
    return 0.0f;
  }

  const size_t left_index = static_cast<size_t>(voice.sample_position);
  const size_t right_index =
      std::min(left_index + 1, static_cast<size_t>(voice.sample_length - 1));
  const float fraction = voice.sample_position - static_cast<float>(left_index);
  const float left_value = static_cast<float>(voice.sample_data[left_index]) / 127.0f;
  const float right_value = static_cast<float>(voice.sample_data[right_index]) / 127.0f;
  return left_value + (right_value - left_value) * fraction;
}

Voice make_voice(const ScheduledNote& note, RenderInstrument instrument) {
  Voice voice = {};
  voice.age_samples = 0;
  voice.sustain_samples = std::max<uint32_t>(1, note.sustain_samples);
  voice.total_samples = std::max<uint32_t>(voice.sustain_samples + 1, note.total_samples);
  voice.gain = note.gain;
  voice.instrument = instrument;
  voice.midi = note.midi;
  voice.last_output = 0.0f;
  voice.previous_sample = 0.0f;
  voice.body_state = 0.0f;

  voice.sample_position = 0.0f;
  if (instrument == RenderInstrument::PianoSample) {
    const auto& sample = find_best_sample(note.midi);
    voice.sample_length = sample.length;
    voice.sample_data = sample.data;
    const float semitone_ratio =
        std::pow(2.0f, (static_cast<float>(note.midi) -
                        static_cast<float>(sample.midi)) /
                       12.0f);
    voice.sample_step = (static_cast<float>(PianoSampleData::kSampleRate) /
                         static_cast<float>(kSampleRate)) *
                        semitone_ratio;
  } else {
    const auto& sample = find_best_musicbox_sample(note.midi);
    voice.sample_length = sample.length;
    voice.sample_data = sample.data;
    const float semitone_ratio =
        std::pow(2.0f, (static_cast<float>(note.midi) -
                        static_cast<float>(sample.midi)) /
                       12.0f);
    voice.sample_step = (static_cast<float>(MusicBoxSampleData::kSampleRate) /
                         static_cast<float>(kSampleRate)) *
                        semitone_ratio;
    const uint32_t natural_samples = std::max<uint32_t>(
        1, static_cast<uint32_t>(static_cast<float>(sample.length) / voice.sample_step));
    voice.sustain_samples = natural_samples;
    voice.total_samples = natural_samples;
  }
  return voice;
}

float piano_sample_voice(Voice& voice) {
  const float age_seconds =
      static_cast<float>(voice.age_samples) / static_cast<float>(kSampleRate);
  const float sustain_seconds =
      static_cast<float>(voice.sustain_samples) / static_cast<float>(kSampleRate);
  const float total_seconds =
      static_cast<float>(voice.total_samples) / static_cast<float>(kSampleRate);

  float envelope = 0.0f;
  if (age_seconds < 0.006f) {
    envelope = age_seconds / 0.006f;
  } else if (age_seconds < sustain_seconds) {
    envelope = 0.92f * std::exp(-0.70f * std::max(0.0f, age_seconds - 0.006f)) + 0.08f;
  } else {
    const float release_duration = std::max(0.10f, total_seconds - sustain_seconds);
    const float release_progress = (age_seconds - sustain_seconds) / release_duration;
    envelope = std::max(0.0f, 0.20f * (1.0f - release_progress));
  }

  float sample = 0.0f;
  sample = sample_voice_value(voice);

  voice.sample_position += voice.sample_step;
  voice.last_output = voice.last_output * 0.12f + sample * 0.88f;
  voice.age_samples++;
  return voice.last_output * envelope * voice.gain;
}

float musicbox_voice_sample(Voice& voice) {
  const float age_seconds =
      static_cast<float>(voice.age_samples) / static_cast<float>(kSampleRate);
  const float raw_pitch_decay_bias =
      (72.0f - static_cast<float>(voice.midi)) / 36.0f;
  const float pitch_decay_bias =
      std::max(-0.35f, std::min(0.55f, raw_pitch_decay_bias));
  const float decay_seconds = 0.52f + pitch_decay_bias * 0.14f;
  const float decay_envelope = std::exp(-age_seconds / std::max(0.18f, decay_seconds));
  const float attack_seconds = 0.0007f;
  const float attack_envelope =
      age_seconds < attack_seconds ? (age_seconds / attack_seconds) : 1.0f;
  const float attack_window =
      age_seconds < 0.013f ? (1.0f - age_seconds / 0.013f) : 0.0f;

  const float dry_sample = sample_voice_value(voice);
  voice.body_state = voice.body_state * 0.77f + dry_sample * 0.23f;
  const float body_sample = voice.body_state;
  const float sparkle = dry_sample - body_sample;
  const float pick_click = (dry_sample - voice.previous_sample) * 1.22f * attack_window;
  voice.previous_sample = dry_sample;
  voice.sample_position += voice.sample_step;
  const float shaped_sample =
      dry_sample * 0.80f + body_sample * 0.08f + sparkle * 0.40f + pick_click;
  voice.last_output = voice.last_output * 0.02f + shaped_sample * 0.98f;
  voice.age_samples++;
  return voice.last_output * attack_envelope * decay_envelope * voice.gain * 1.04f;
}

float render_voice_sample(Voice& voice) {
  if (voice.instrument == RenderInstrument::MusicBox) {
    return musicbox_voice_sample(voice);
  }
  return piano_sample_voice(voice);
}

float shape_musicbox_master(float sample, MusicBoxMasterState& state) {
  // Small full-range speakers exaggerate muddy low mids and lose the wood/metal
  // box character. Shape the sum toward a brighter mechanical music-box response.
  const float highpassed =
      sample - state.hp_input + state.hp_output * 0.92f;
  state.hp_input = sample;
  state.hp_output = highpassed;

  state.body_lowpass += (highpassed - state.body_lowpass) * 0.07f;
  const float presence_input = highpassed - state.body_lowpass;
  state.presence_band += (presence_input - state.presence_band) * 0.38f;

  state.air_lowpass += (highpassed - state.air_lowpass) * 0.28f;
  const float air = highpassed - state.air_lowpass;

  return highpassed * 0.79f + state.presence_band * 0.37f + air * 0.15f;
}

std::vector<ScheduledNote> build_schedule_from_events(RenderInstrument instrument,
                                                      const GenericMidiEvent* events,
                                                      size_t event_count,
                                                      uint32_t ticks_per_beat,
                                                      uint32_t tempo_us_per_beat) {
  std::vector<ScheduledNote> notes;
  notes.reserve(event_count);
  uint32_t base_start_tick = UINT32_MAX;

  for (size_t index = 0; index < event_count; ++index) {
    const auto& event = events[index];
    if (event.midi == 0 || event.duration_tick == 0) {
      continue;
    }
    base_start_tick = std::min(base_start_tick, event.start_tick);
  }

  if (base_start_tick == UINT32_MAX) {
    base_start_tick = 0;
  }

  for (size_t index = 0; index < event_count; ++index) {
    const auto& event = events[index];
    if (event.midi == 0 || event.duration_tick == 0) {
      continue;
    }

    const uint32_t start_sample =
        static_cast<uint32_t>((static_cast<uint64_t>(event.start_tick - base_start_tick) *
                               tempo_us_per_beat *
                               kSampleRate) /
                              (ticks_per_beat * 1000000ULL));
    const uint32_t note_samples =
        static_cast<uint32_t>((static_cast<uint64_t>(event.duration_tick) * tempo_us_per_beat *
                               kSampleRate) /
                              (ticks_per_beat * 1000000ULL));
    if (note_samples == 0) {
      continue;
    }

    const bool melody_role = event.role == 0;
    const bool strong_beat =
        (event.start_tick % (ticks_per_beat * 4U)) == 0U;
    const bool medium_beat =
        (event.start_tick % (ticks_per_beat * 2U)) == 0U;

    const uint32_t sustain_samples =
        std::max<uint32_t>(1, note_samples * (melody_role ? 99U : 94U) / 100U);
    const uint32_t total_samples =
        sustain_samples +
        (melody_role ? (kSampleRate * 30U / 100U) : (kSampleRate * 18U / 100U));
    const float velocity_gain =
        0.68f + (static_cast<float>(event.velocity) / 127.0f) * 0.46f;
    const float role_gain =
        instrument == RenderInstrument::MusicBox
            ? (melody_role ? 0.32f : 0.08f)
            : (melody_role ? 0.30f : 0.13f);
    const float beat_gain = strong_beat ? 1.12f : (medium_beat ? 1.05f : 1.0f);

    notes.push_back({
        start_sample,
        sustain_samples,
        total_samples,
        event.midi,
        event.role,
        role_gain * velocity_gain * beat_gain,
    });
  }

  std::sort(notes.begin(), notes.end(),
            [](const ScheduledNote& lhs, const ScheduledNote& rhs) {
              if (lhs.start_sample != rhs.start_sample) {
                return lhs.start_sample < rhs.start_sample;
              }
              return lhs.midi > rhs.midi;
            });
  return notes;
}

bool play_song_from_events(RenderInstrument instrument, const char* label,
                           const GenericMidiEvent* events, size_t event_count,
                           uint32_t ticks_per_beat, uint32_t tempo_us_per_beat) {
  const std::vector<ScheduledNote> notes =
      build_schedule_from_events(instrument, events, event_count, ticks_per_beat,
                                 tempo_us_per_beat);
  if (notes.empty()) {
    Serial.println("Melody playback failed: empty song schedule");
    return false;
  }

  uint32_t total_samples = 0;
  for (const ScheduledNote& note : notes) {
    total_samples = std::max(total_samples, note.start_sample + note.total_samples);
  }
  total_samples += kSampleRate / 3U;

  if (!audio_begin_stream(kSampleRate)) {
    return false;
  }

  Serial.printf("%s %s playback: events=%u duration_ms=%u\n",
                label,
                instrument == RenderInstrument::MusicBox ? "musicbox" : "piano",
                static_cast<unsigned>(notes.size()),
                static_cast<unsigned>((total_samples * 1000ULL) / kSampleRate));

  std::vector<Voice> active_voices;
  active_voices.reserve(24);
  std::vector<int16_t> pcm_chunk(kStreamChunkSamples, 0);
  MusicBoxMasterState musicbox_master_state = {};
  size_t next_note_index = 0;
  uint32_t rendered_samples = 0;
  bool interrupted = false;

  while (rendered_samples < total_samples) {
    if (g_service_callback != nullptr) {
      g_service_callback();
    }
    if (g_stop_requested) {
      interrupted = true;
      break;
    }

    const size_t chunk_samples =
        std::min(kStreamChunkSamples,
                 static_cast<size_t>(total_samples - rendered_samples));

    for (size_t i = 0; i < chunk_samples; ++i) {
      while (next_note_index < notes.size() &&
             notes[next_note_index].start_sample == rendered_samples) {
        const ScheduledNote& scheduled_note = notes[next_note_index];
        const bool is_musicbox_accompaniment =
            instrument == RenderInstrument::MusicBox && scheduled_note.role != 0;
        if (!is_musicbox_accompaniment || g_musicbox_accompaniment_enabled) {
          active_voices.push_back(make_voice(scheduled_note, instrument));
        }
        ++next_note_index;
      }

      float mixed = 0.0f;
      for (Voice& voice : active_voices) {
        mixed += render_voice_sample(voice);
      }

      if (instrument == RenderInstrument::MusicBox) {
        mixed = shape_musicbox_master(mixed, musicbox_master_state);
      }

      active_voices.erase(
          std::remove_if(active_voices.begin(), active_voices.end(),
                         [](const Voice& voice) {
                           return voice.age_samples >= voice.total_samples;
                         }),
          active_voices.end());

      const float drive = instrument == RenderInstrument::MusicBox ? 0.76f : 1.55f;
      const float output =
          instrument == RenderInstrument::MusicBox ? 26600.0f : 30000.0f;
      const float shaped = std::tanh(mixed * drive);
      pcm_chunk[i] = static_cast<int16_t>(shaped * output);
      ++rendered_samples;
    }

    if (!audio_write_pcm_16_mono(pcm_chunk.data(), chunk_samples)) {
      audio_end_stream();
      Serial.println("Melody playback failed: stream write");
      return false;
    }
  }

  audio_end_stream();
  if (interrupted) {
    Serial.printf("%s playback interrupted\n", label);
    return true;
  }
  Serial.printf("%s playback complete\n", label);
  return true;
}

}  // namespace

const char* melody_instrument_name(MelodyInstrument instrument) {
  switch (instrument) {
    case MelodyInstrument::Piano:
      return "piano";
    case MelodyInstrument::MusicBox:
      return "musicbox";
    case MelodyInstrument::Guitar:
      return "guitar";
  }
  return "unknown";
}

const char* melody_song_name(MelodySong song) {
  switch (song) {
    case MelodySong::Canon:
      return "canon";
    case MelodySong::ToriNoUta:
      return "tori";
    case MelodySong::TheTruthThatYouLeave:
      return "truth";
    case MelodySong::MySoul:
      return "juebieshu";
    case MelodySong::QiFengLe:
      return "qifeng";
    case MelodySong::ChuanYueShiKongDeSiNian:
      return "sinian";
    case MelodySong::CastleInTheSky:
      return "castle";
  }
  return "unknown";
}

void melody_set_service_callback(MelodyServiceCallback callback) {
  g_service_callback = callback;
}

void melody_request_stop() {
  g_stop_requested = true;
}

void melody_clear_stop_request() {
  g_stop_requested = false;
}

bool melody_stop_requested() {
  return g_stop_requested;
}

void melody_set_musicbox_accompaniment_enabled(bool enabled) {
  g_musicbox_accompaniment_enabled = enabled;
}

bool melody_musicbox_accompaniment_enabled() {
  return g_musicbox_accompaniment_enabled;
}

bool melody_parse_instrument(const String& value, MelodyInstrument& instrument) {
  if (value.equalsIgnoreCase("piano")) {
    instrument = MelodyInstrument::Piano;
    return true;
  }
  if (value.equalsIgnoreCase("musicbox") || value.equalsIgnoreCase("box") ||
      value.equalsIgnoreCase("mbox")) {
    instrument = MelodyInstrument::MusicBox;
    return true;
  }
  if (value.equalsIgnoreCase("guitar")) {
    instrument = MelodyInstrument::Guitar;
    return true;
  }
  return false;
}

bool melody_parse_song(const String& value, MelodySong& song) {
  if (value.equalsIgnoreCase("canon") || value.equalsIgnoreCase("kanon")) {
    song = MelodySong::Canon;
    return true;
  }
  if (value.equalsIgnoreCase("tori") || value.equalsIgnoreCase("torinouta") ||
      value.equalsIgnoreCase("bird") || value.equalsIgnoreCase("birdsong")) {
    song = MelodySong::ToriNoUta;
    return true;
  }
  if (value.equalsIgnoreCase("truth") || value.equalsIgnoreCase("thruth") ||
      value.equalsIgnoreCase("thetruththatyouleave") ||
      value.equalsIgnoreCase("thethruththatyouleave")) {
    song = MelodySong::TheTruthThatYouLeave;
    return true;
  }
  if (value.equalsIgnoreCase("juebieshu") || value.equalsIgnoreCase("juebie") ||
      value.equalsIgnoreCase("mysoul") || value.equalsIgnoreCase("my") ||
      value.equalsIgnoreCase("mysoulyourbeats") ||
      value.equalsIgnoreCase("mysoulyourbeats")) {
    song = MelodySong::MySoul;
    return true;
  }
  if (value.equalsIgnoreCase("qifeng") || value.equalsIgnoreCase("qifengle") ||
      value.equalsIgnoreCase("qifei") || value.equalsIgnoreCase("qifeile")) {
    song = MelodySong::QiFengLe;
    return true;
  }
  if (value.equalsIgnoreCase("sinian") ||
      value.equalsIgnoreCase("chuanyuesikongdesinian") ||
      value.equalsIgnoreCase("chuanyuesikong")) {
    song = MelodySong::ChuanYueShiKongDeSiNian;
    return true;
  }
  if (value.equalsIgnoreCase("castle") || value.equalsIgnoreCase("castleinthesky") ||
      value.equalsIgnoreCase("sky") || value.equalsIgnoreCase("laputa")) {
    song = MelodySong::CastleInTheSky;
    return true;
  }
  return false;
}

bool melody_play_song(MelodyInstrument instrument, MelodySong song) {
  RenderInstrument render_instrument;
  if (instrument == MelodyInstrument::MusicBox) {
    render_instrument = RenderInstrument::MusicBox;
  } else if (instrument == MelodyInstrument::Piano) {
    render_instrument = RenderInstrument::PianoSample;
  } else {
    Serial.println("Only piano and musicbox are available");
    return false;
  }

  if (song == MelodySong::Canon) {
    return play_song_from_events(
        render_instrument, "canon",
        reinterpret_cast<const GenericMidiEvent*>(CanonMidiData::kEvents),
        CanonMidiData::kEventCount, CanonMidiData::kTicksPerBeat,
        CanonMidiData::kTempoMicrosecondsPerBeat);
  }
  if (song == MelodySong::ToriNoUta) {
    return play_song_from_events(
        render_instrument, "tori",
        reinterpret_cast<const GenericMidiEvent*>(ToriNoUtaMidiData::kEvents),
        ToriNoUtaMidiData::kEventCount, ToriNoUtaMidiData::kTicksPerBeat,
        ToriNoUtaMidiData::kTempoMicrosecondsPerBeat);
  }
  if (song == MelodySong::TheTruthThatYouLeave) {
    return play_song_from_events(
        render_instrument, "truth",
        reinterpret_cast<const GenericMidiEvent*>(TruthThatYouLeaveMidiData::kEvents),
        TruthThatYouLeaveMidiData::kEventCount, TruthThatYouLeaveMidiData::kTicksPerBeat,
        TruthThatYouLeaveMidiData::kTempoMicrosecondsPerBeat);
  }
  if (song == MelodySong::MySoul) {
    return play_song_from_events(
        render_instrument, "juebieshu",
        reinterpret_cast<const GenericMidiEvent*>(JueBieShuMidiData::kEvents),
        JueBieShuMidiData::kEventCount, JueBieShuMidiData::kTicksPerBeat,
        JueBieShuMidiData::kTempoMicrosecondsPerBeat);
  }
  if (song == MelodySong::QiFengLe) {
    return play_song_from_events(
        render_instrument, "qifeng",
        reinterpret_cast<const GenericMidiEvent*>(QiFengLeMidiData::kEvents),
        QiFengLeMidiData::kEventCount, QiFengLeMidiData::kTicksPerBeat,
        QiFengLeMidiData::kTempoMicrosecondsPerBeat);
  }
  if (song == MelodySong::ChuanYueShiKongDeSiNian) {
    return play_song_from_events(
        render_instrument, "sinian",
        reinterpret_cast<const GenericMidiEvent*>(
            ChuanYueShiKongDeSiNianMidiData::kEvents),
        ChuanYueShiKongDeSiNianMidiData::kEventCount,
        ChuanYueShiKongDeSiNianMidiData::kTicksPerBeat,
        ChuanYueShiKongDeSiNianMidiData::kTempoMicrosecondsPerBeat);
  }

  Serial.println(
      "Only canon, tori, truth, juebieshu, qifeng, and sinian are available");
  return false;
}

bool melody_play_random() {
  return melody_play_song(MelodyInstrument::MusicBox, MelodySong::Canon);
}
