#include <Arduino.h>

#include <algorithm>
#include <vector>

#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>

#include "app_config.h"
#include "app_wifi.h"
#include "asr.h"
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
WebServer g_http_server(80);
bool g_http_server_started = false;
bool g_mdns_started = false;
constexpr char kMdnsHostName[] = "airobot";
std::vector<uint8_t> g_uploaded_voice_audio;
String g_uploaded_voice_mime_type = "audio/webm";
bool g_uploaded_voice_too_large = false;
String g_voice_pipeline_stage = "idle";
std::vector<ChatMessage> g_voice_conversation_history;
constexpr size_t kVoiceConversationHistoryLimit = 6;
constexpr size_t kSafeAsrAudioBytes = 64 * 1024;

constexpr const char kControlPage[] PROGMEM = R"HTML(
<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
  <title>AIRobot</title>
  <style>
    :root { color-scheme: dark; --bg:#0b1220; --panel:#121c30; --line:#26344d; --text:#f2f7ff; --muted:#94a3b8; --accent:#38bdf8; --accent2:#22c55e; --danger:#ef4444; }
    * { box-sizing:border-box; }
    body { margin:0; font-family:"Segoe UI",sans-serif; background:linear-gradient(180deg,#09111d,#0b1220 35%,#0f1b31); color:var(--text); }
    .wrap { max-width:760px; margin:0 auto; padding:20px 14px 28px; }
    .hero { margin-bottom:14px; }
    .hero h1 { margin:0; font-size:28px; }
    .hero p { margin:6px 0 0; color:var(--muted); }
    .card { background:rgba(18,28,48,.92); border:1px solid var(--line); border-radius:18px; padding:14px; margin-bottom:12px; box-shadow:0 10px 30px rgba(0,0,0,.22); }
    .grid { display:grid; grid-template-columns:repeat(2,minmax(0,1fr)); gap:10px; }
    .row { display:flex; gap:10px; align-items:center; }
    .stack { display:flex; flex-direction:column; gap:10px; }
    button, select, input, textarea { width:100%; border-radius:12px; border:1px solid var(--line); background:#0b1324; color:var(--text); padding:12px 14px; font:inherit; }
    button { background:linear-gradient(135deg,#0ea5e9,#2563eb); border:none; font-weight:600; }
    button.secondary { background:linear-gradient(135deg,#1e293b,#334155); }
    button.success { background:linear-gradient(135deg,#16a34a,#22c55e); }
    button.danger { background:linear-gradient(135deg,#dc2626,#ef4444); }
    textarea { min-height:84px; resize:vertical; }
    label { display:block; color:var(--muted); font-size:13px; margin-bottom:6px; }
    pre { margin:0; white-space:pre-wrap; word-break:break-word; font-family:Consolas,monospace; background:#09111d; border-radius:12px; padding:12px; max-height:240px; overflow:auto; }
    .status { display:grid; grid-template-columns:repeat(2,minmax(0,1fr)); gap:8px; }
    .pill { padding:10px 12px; border-radius:12px; background:#0a1323; border:1px solid var(--line); }
    .pill strong { display:block; font-size:12px; color:var(--muted); margin-bottom:4px; }
    .mono { font-family:Consolas,monospace; }
    @media (max-width:640px) { .grid, .status { grid-template-columns:1fr; } .hero h1 { font-size:24px; } }
  </style>
</head>
<body>
  <div class="wrap">
    <div class="hero">
      <h1>AIRobot Mobile Console</h1>
      <p>通过手机控制音量、歌曲、TTS 和聊天。</p>
    </div>

    <div class="card">
      <div class="status" id="status"></div>
    </div>

    <div class="card stack">
      <div class="grid">
        <div>
          <label for="song">歌曲</label>
          <select id="song">
            <option value="canon">卡农</option>
            <option value="tori">鸟之诗</option>
            <option value="truth">风居住的街道</option>
            <option value="juebieshu">诀别书</option>
            <option value="qifeng">起风了</option>
            <option value="sinian">穿越时空的思念</option>
          </select>
        </div>
        <div>
          <label for="instrument">音色</label>
          <select id="instrument">
            <option value="musicbox">八音盒</option>
            <option value="piano">钢琴</option>
          </select>
        </div>
      </div>
      <div class="grid">
        <button onclick="playMelody()">播放音乐</button>
        <button class="danger" onclick="post('/api/melody/stop')">停止音乐</button>
      </div>
      <div class="grid">
        <button class="secondary" onclick="toggleAccomp(true)">伴奏开</button>
        <button class="secondary" onclick="toggleAccomp(false)">伴奏关</button>
      </div>
    </div>

    <div class="card stack">
      <div>
        <label for="volume">音量</label>
        <input id="volume" type="range" min="0" max="100" value="70" oninput="document.getElementById('volumeText').value=this.value">
      </div>
      <div class="row">
        <input id="volumeText" type="number" min="0" max="100" value="70">
        <button class="success" onclick="setVolume()">应用音量</button>
      </div>
    </div>

    <div class="card stack">
      <div>
        <label for="sayText">TTS</label>
        <textarea id="sayText" placeholder="输入要说的话"></textarea>
      </div>
      <button onclick="sendSay()">发送 TTS</button>
    </div>

    <div class="card stack">
      <div>
        <label for="askText">聊天</label>
        <textarea id="askText" placeholder="输入想问机器人的话"></textarea>
      </div>
      <button onclick="sendAsk()">发送聊天</button>
    </div>

    <div class="card stack">
      <div class="grid">
        <button id="voiceHold" class="success">Hold To Talk</button>
        <button id="voiceCancel" class="danger" onclick="cancelVoice()" disabled>Cancel</button>
      </div>
      <input id="voiceFile" type="file" accept=".m4a,.mp3,.wav,.aac,audio/*" style="display:none">
      <button class="secondary" onclick="fallbackVoicePick()">Pick Audio File</button>
      <pre id="voiceStatus">Press and hold to talk</pre>
    </div>

    <div class="card stack">
      <div>
        <label for="commandText">原始命令</label>
        <input id="commandText" placeholder="例如 help / wifi / emo smile">
      </div>
      <button class="secondary" onclick="sendCommand()">发送命令</button>
    </div>

    <div class="card stack">
      <div class="row">
        <button class="secondary" onclick="loadStatus()">刷新状态</button>
      </div>
      <pre id="result">准备就绪</pre>
    </div>
  </div>
  <script>
    let mediaRecorder = null;
    let mediaStream = null;
    let recordedChunks = [];
    let audioContext = null;
    let audioSourceNode = null;
    let scriptProcessor = null;
    let pcmChunks = [];
    let pcmSampleRate = 16000;
    let usingPcmRecorder = false;
    let voicePointerActive = false;
    let voiceStartedAt = 0;
    function setVoiceStatus(text) {
      document.getElementById('voiceStatus').textContent = text;
    }
    function setVoiceButtons(recording) {
      document.getElementById('voiceHold').disabled = recording;
      document.getElementById('voiceCancel').disabled = !recording;
    }
    async function post(path, data) {
      const body = new URLSearchParams(data || {});
      const res = await fetch(path, { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' }, body });
      const text = await res.text();
      document.getElementById('result').textContent = text || 'OK';
      loadStatus();
      return text;
    }
    async function postBinary(path, blob) {
      const formData = new FormData();
      const extension = (blob.type || 'audio/webm').includes('mp4') ? 'm4a' : 'webm';
      formData.append('audio', blob, 'voice.' + extension);
      const res = await fetch(path, { method: 'POST', body: formData });
      const text = await res.text();
      document.getElementById('result').textContent = text || 'OK';
      loadStatus();
      return text;
    }
    async function uploadVoiceBlob(blob) {
      if (!blob || !blob.size) {
        setVoiceStatus('No audio captured.');
        return;
      }
      setVoiceStatus('Uploading voice...');
      const raw = await postBinary('/api/asr-chat', blob);
      try {
        const parsed = JSON.parse(raw);
        setVoiceStatus('You: ' + (parsed.transcript || '') + '\nRobot: ' + (parsed.reply || ''));
      } catch (_err) {
        setVoiceStatus(raw);
      }
    }
    function mergePcmChunks(chunks) {
      let totalLength = 0;
      for (const chunk of chunks) {
        totalLength += chunk.length;
      }
      const merged = new Float32Array(totalLength);
      let offset = 0;
      for (const chunk of chunks) {
        merged.set(chunk, offset);
        offset += chunk.length;
      }
      return merged;
    }
    function downsampleBuffer(buffer, inputSampleRate, outputSampleRate) {
      if (outputSampleRate >= inputSampleRate) {
        return buffer;
      }
      const ratio = inputSampleRate / outputSampleRate;
      const newLength = Math.max(1, Math.round(buffer.length / ratio));
      const result = new Float32Array(newLength);
      let offsetResult = 0;
      let offsetBuffer = 0;
      while (offsetResult < result.length) {
        const nextOffsetBuffer = Math.min(buffer.length, Math.round((offsetResult + 1) * ratio));
        let accum = 0;
        let count = 0;
        for (let i = offsetBuffer; i < nextOffsetBuffer; i++) {
          accum += buffer[i];
          count += 1;
        }
        result[offsetResult] = count ? (accum / count) : 0;
        offsetResult += 1;
        offsetBuffer = nextOffsetBuffer;
      }
      return result;
    }
    function encodeWavBlob(samples, sampleRate) {
      const buffer = new ArrayBuffer(44 + samples.length * 2);
      const view = new DataView(buffer);
      const writeString = (offset, value) => {
        for (let i = 0; i < value.length; i++) {
          view.setUint8(offset + i, value.charCodeAt(i));
        }
      };
      writeString(0, 'RIFF');
      view.setUint32(4, 36 + samples.length * 2, true);
      writeString(8, 'WAVE');
      writeString(12, 'fmt ');
      view.setUint32(16, 16, true);
      view.setUint16(20, 1, true);
      view.setUint16(22, 1, true);
      view.setUint32(24, sampleRate, true);
      view.setUint32(28, sampleRate * 2, true);
      view.setUint16(32, 2, true);
      view.setUint16(34, 16, true);
      writeString(36, 'data');
      view.setUint32(40, samples.length * 2, true);
      let offset = 44;
      for (let i = 0; i < samples.length; i++, offset += 2) {
        const sample = Math.max(-1, Math.min(1, samples[i]));
        view.setInt16(offset, sample < 0 ? sample * 0x8000 : sample * 0x7FFF, true);
      }
      return new Blob([view], { type: 'audio/wav' });
    }
    function cleanupPcmRecorder() {
      if (scriptProcessor) {
        scriptProcessor.disconnect();
        scriptProcessor.onaudioprocess = null;
        scriptProcessor = null;
      }
      if (audioSourceNode) {
        audioSourceNode.disconnect();
        audioSourceNode = null;
      }
      if (audioContext) {
        audioContext.close();
        audioContext = null;
      }
      if (mediaStream) {
        mediaStream.getTracks().forEach((track) => track.stop());
        mediaStream = null;
      }
      pcmChunks = [];
      usingPcmRecorder = false;
    }
    async function startPcmVoiceFallback() {
      mediaStream = await navigator.mediaDevices.getUserMedia({ audio: true });
      const AudioContextCtor = window.AudioContext || window.webkitAudioContext;
      if (!AudioContextCtor) {
        throw new Error('Web Audio API is not available.');
      }
      audioContext = new AudioContextCtor();
      pcmSampleRate = audioContext.sampleRate || 16000;
      pcmChunks = [];
      audioSourceNode = audioContext.createMediaStreamSource(mediaStream);
      scriptProcessor = audioContext.createScriptProcessor(4096, 1, 1);
      scriptProcessor.onaudioprocess = (event) => {
        const channelData = event.inputBuffer.getChannelData(0);
        pcmChunks.push(new Float32Array(channelData));
      };
      audioSourceNode.connect(scriptProcessor);
      scriptProcessor.connect(audioContext.destination);
      usingPcmRecorder = true;
      setVoiceButtons(true);
      voiceStartedAt = Date.now();
      setVoiceStatus('Recording... release to send');
    }
    async function loadStatus() {
      const res = await fetch('/api/status');
      const data = await res.json();
      document.getElementById('volume').value = data.volume;
      document.getElementById('volumeText').value = data.volume;
      const status = [
        ['WiFi', data.wifi_connected ? ('在线 ' + (data.ip || '')) : '离线'],
        ['音量', data.volume + '%'],
        ['伴奏', data.musicbox_accompaniment ? '开启' : '关闭'],
        ['用户', data.user_name || '未设置'],
        ['机器人', data.robot_name || '未设置'],
        ['当前循环', data.loop_song + ' / ' + data.loop_instrument]
      ];
      document.getElementById('status').innerHTML = status.map(([k,v]) => '<div class="pill"><strong>'+k+'</strong><span class="mono">'+v+'</span></div>').join('');
    }
    function playMelody() {
      return post('/api/melody/play', { song: document.getElementById('song').value, instrument: document.getElementById('instrument').value });
    }
    function toggleAccomp(enabled) {
      return post('/api/accompaniment', { enabled: enabled ? 'on' : 'off' });
    }
    function setVolume() {
      return post('/api/volume', { value: document.getElementById('volumeText').value });
    }
    function sendSay() {
      return post('/api/tts', { text: document.getElementById('sayText').value });
    }
    function sendAsk() {
      return post('/api/chat', { text: document.getElementById('askText').value });
    }
    function sendCommand() {
      return post('/api/command', { text: document.getElementById('commandText').value });
    }
    const baseLoadStatus = loadStatus;
    loadStatus = async function() {
      await baseLoadStatus();
      const res = await fetch('/api/status');
      const data = await res.json();
      const status = document.getElementById('status');
      if (status && !status.textContent.includes('Voice')) {
        status.innerHTML += '<div class="pill"><strong>Voice</strong><span class="mono">' + (data.voice_stage || 'idle') + '</span></div>';
      }
      const stageMap = {
        idle: 'Press and hold to talk',
        recording: 'Recording...',
        uploading: 'Uploading...',
        recognizing: 'Recognizing...',
        thinking: 'Thinking...',
        speaking: 'Speaking...'
      };
      if (!mediaRecorder) {
        setVoiceStatus(stageMap[data.voice_stage] || 'Press and hold to talk');
      }
    }
    function chooseRecorderMimeType() {
      const candidates = [
        'audio/webm;codecs=opus',
        'audio/mp4',
        'audio/webm',
        'audio/ogg;codecs=opus'
      ];
      for (const type of candidates) {
        if (window.MediaRecorder && MediaRecorder.isTypeSupported && MediaRecorder.isTypeSupported(type)) {
          return type;
        }
      }
      return '';
    }
    async function startVoice() {
      if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) {
        setVoiceStatus('Microphone access is not supported here. Use Pick Audio File.');
        return;
      }
      try {
        const mimeType = chooseRecorderMimeType();
        if (!window.MediaRecorder) {
          await startPcmVoiceFallback();
          return;
        }
        mediaStream = await navigator.mediaDevices.getUserMedia({ audio: true });
        recordedChunks = [];
        mediaRecorder = mimeType ? new MediaRecorder(mediaStream, { mimeType, audioBitsPerSecond: 24000 }) : new MediaRecorder(mediaStream);
        mediaRecorder.ondataavailable = (event) => {
          if (event.data && event.data.size > 0) {
            recordedChunks.push(event.data);
          }
        };
        mediaRecorder.onerror = (event) => {
          setVoiceStatus('Recorder error: ' + (event.error ? event.error.message : 'unknown'));
          setVoiceButtons(false);
        };
        mediaRecorder.onstop = async () => {
          setVoiceButtons(false);
          try {
            const blob = new Blob(recordedChunks, { type: mediaRecorder.mimeType || 'audio/webm' });
            await uploadVoiceBlob(blob);
          } catch (err) {
            setVoiceStatus('Voice upload failed: ' + (err && err.message ? err.message : err));
          } finally {
            if (mediaStream) {
              mediaStream.getTracks().forEach((track) => track.stop());
            }
            mediaStream = null;
            mediaRecorder = null;
            recordedChunks = [];
            voicePointerActive = false;
          }
        };
        mediaRecorder.start();
        setVoiceButtons(true);
        voiceStartedAt = Date.now();
        setVoiceStatus('Recording... release to send');
      } catch (err) {
        setVoiceButtons(false);
        setVoiceStatus('Microphone start failed: ' + (err && err.message ? err.message : err));
      }
    }
    function stopVoice() {
      if (usingPcmRecorder) {
        setVoiceStatus('Processing...');
        setVoiceButtons(false);
        try {
          const merged = mergePcmChunks(pcmChunks);
          const downsampled = downsampleBuffer(merged, pcmSampleRate, 16000);
          const wavBlob = encodeWavBlob(downsampled, 16000);
          cleanupPcmRecorder();
          uploadVoiceBlob(wavBlob).catch((err) => {
            setVoiceStatus('Voice upload failed: ' + (err && err.message ? err.message : err));
          });
        } catch (err) {
          cleanupPcmRecorder();
          setVoiceStatus('PCM record failed: ' + (err && err.message ? err.message : err));
        }
        return;
      }
      if (!mediaRecorder) {
        return;
      }
      setVoiceStatus('Processing...');
      if (mediaRecorder.state !== 'inactive') {
        mediaRecorder.stop();
      }
    }
    function cancelVoice() {
      if (usingPcmRecorder) {
        cleanupPcmRecorder();
        setVoiceButtons(false);
        setVoiceStatus('Voice canceled');
        return;
      }
      if (mediaRecorder && mediaRecorder.state !== 'inactive') {
        recordedChunks = [];
        setVoiceStatus('Voice canceled');
        mediaRecorder.stop();
      }
    }
    function bindHoldToTalk() {
      const hold = document.getElementById('voiceHold');
      const start = async (event) => {
        event.preventDefault();
        if (voicePointerActive || mediaRecorder) {
          return;
        }
        voicePointerActive = true;
        await startVoice();
      };
      const stop = (event) => {
        if (event) {
          event.preventDefault();
        }
        if (!voicePointerActive) {
          return;
        }
        voicePointerActive = false;
        if (mediaRecorder && Date.now() - voiceStartedAt < 250) {
          setTimeout(() => stopVoice(), 250);
        } else {
          stopVoice();
        }
      };
      hold.addEventListener('touchstart', start, { passive: false });
      hold.addEventListener('touchend', stop, { passive: false });
      hold.addEventListener('touchcancel', stop, { passive: false });
      hold.addEventListener('mousedown', start);
      hold.addEventListener('mouseup', stop);
      hold.addEventListener('mouseleave', stop);
    }
    function fallbackVoicePick() {
      document.getElementById('voiceFile').click();
    }
    document.getElementById('voiceFile').addEventListener('change', async (event) => {
      const file = event.target.files && event.target.files[0];
      if (!file) {
        return;
      }
      try {
        await uploadVoiceBlob(file);
      } catch (err) {
        setVoiceStatus('Voice upload failed: ' + (err && err.message ? err.message : err));
      } finally {
        event.target.value = '';
      }
    });
    bindHoldToTalk();
    loadStatus();
    setInterval(loadStatus, 5000);
  </script>
</body>
</html>
)HTML";

constexpr MelodySong kLoopPlaylist[] = {
    MelodySong::Canon,
    MelodySong::ToriNoUta,
    MelodySong::TheTruthThatYouLeave,
    MelodySong::MySoul,
    MelodySong::QiFengLe,
    MelodySong::ChuanYueShiKongDeSiNian,
};

bool handle_tts_command(const String& line);
bool handle_chat_command(const String& line);

String json_escape(const String& value) {
  String escaped;
  escaped.reserve(value.length() + 8);
  for (unsigned i = 0; i < value.length(); ++i) {
    const char ch = value[i];
    if (ch == '\\' || ch == '"') {
      escaped += '\\';
      escaped += ch;
    } else if (ch == '\n') {
      escaped += "\\n";
    } else if (ch == '\r') {
      escaped += "\\r";
    } else if (ch == '\t') {
      escaped += "\\t";
    } else {
      escaped += ch;
    }
  }
  return escaped;
}

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
  Serial.println("  web             -> print mobile control URL");
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

String current_ip_string() {
  if (!wifi_is_connected()) {
    return "";
  }
  return WiFi.localIP().toString();
}

void ensure_mdns() {
  if (g_mdns_started || !wifi_is_connected()) {
    return;
  }

  if (!MDNS.begin(kMdnsHostName)) {
    Serial.println("mDNS start failed");
    return;
  }

  MDNS.addService("http", "tcp", 80);
  g_mdns_started = true;
  Serial.printf("mDNS ready: http://%s.local/\n", kMdnsHostName);
}

String build_status_json() {
  const DeviceProfile& profile = device_profile_get();
  String payload;
  payload.reserve(320);
  payload += "{";
  payload += "\"wifi_connected\":";
  payload += wifi_is_connected() ? "true" : "false";
  payload += ",\"ip\":\"";
  payload += json_escape(current_ip_string());
  payload += "\",\"volume\":";
  payload += String(audio_get_output_volume());
  payload += ",\"musicbox_accompaniment\":";
  payload += melody_musicbox_accompaniment_enabled() ? "true" : "false";
  payload += ",\"melody_playing\":";
  payload += g_melody_playing ? "true" : "false";
  payload += ",\"loop_song\":\"";
  payload += json_escape(String(melody_song_name(g_loop_song)));
  payload += "\",\"loop_instrument\":\"";
  payload += json_escape(String(melody_instrument_name(g_loop_instrument)));
  payload += "\",\"user_name\":\"";
  payload += json_escape(profile.user_name);
  payload += "\",\"robot_name\":\"";
  payload += json_escape(profile.robot_name);
  payload += "\",\"voice_stage\":\"";
  payload += json_escape(g_voice_pipeline_stage);
  payload += "\"}";
  return payload;
}

void print_web_url() {
  if (!wifi_is_connected()) {
    Serial.println("Web control unavailable: WiFi disconnected");
    return;
  }
  ensure_mdns();
  if (g_mdns_started) {
    Serial.printf("Mobile control: http://%s.local/\n", kMdnsHostName);
  }
  Serial.printf("Mobile control: http://%s/\n", current_ip_string().c_str());
}

String request_value(const char* key) {
  if (g_http_server.hasArg(key)) {
    return g_http_server.arg(key);
  }
  if (g_http_server.hasArg("plain")) {
    return g_http_server.arg("plain");
  }
  return "";
}

String voice_chat_json_response(const String& transcript, const String& reply_text,
                                bool tts_ok, const String& tts_error) {
  String response = "{";
  response += "\"transcript\":\"";
  response += json_escape(transcript);
  response += "\",\"reply\":\"";
  response += json_escape(reply_text);
  response += "\",\"tts_ok\":";
  response += tts_ok ? "true" : "false";
  if (!tts_ok) {
    response += ",\"tts_error\":\"";
    response += json_escape(tts_error);
    response += "\"";
  }
  response += "}";
  return response;
}

void set_voice_pipeline_stage(const char* stage) {
  g_voice_pipeline_stage = stage;
}

void trim_voice_history() {
  while (g_voice_conversation_history.size() > kVoiceConversationHistoryLimit) {
    g_voice_conversation_history.erase(g_voice_conversation_history.begin());
  }
}

void clear_voice_history() {
  g_voice_conversation_history.clear();
}

void release_uploaded_voice_audio() {
  g_uploaded_voice_audio.clear();
  std::vector<uint8_t>().swap(g_uploaded_voice_audio);
}

String infer_upload_mime_type(const HTTPUpload& upload) {
  String mime = upload.type;
  mime.trim();
  mime.toLowerCase();
  const bool looks_like_audio_type =
      mime.startsWith("audio/") || mime.equals("application/ogg");
  if (looks_like_audio_type) {
    return mime;
  }

  String filename = upload.filename;
  filename.toLowerCase();
  if (filename.endsWith(".m4a") || filename.endsWith(".mp4")) {
    return "audio/mp4";
  }
  if (filename.endsWith(".wav")) {
    return "audio/wav";
  }
  if (filename.endsWith(".aac")) {
    return "audio/aac";
  }
  if (filename.endsWith(".ogg") || filename.endsWith(".opus")) {
    return "audio/ogg";
  }
  if (filename.endsWith(".webm")) {
    return "audio/webm";
  }
  if (filename.endsWith(".mp3")) {
    return "audio/mpeg";
  }
  return "audio/webm";
}

bool run_voice_chat(const String& mime_type, const std::vector<uint8_t>& audio_bytes,
                    String& response_json, int& response_status) {
  response_json = "";
  response_status = 500;

  Serial.printf("Voice chat request: mime=%s bytes=%u heap=%u largest=%u psram=%u\n",
                mime_type.c_str(),
                static_cast<unsigned>(audio_bytes.size()),
                static_cast<unsigned>(ESP.getFreeHeap()),
                static_cast<unsigned>(ESP.getMaxAllocHeap()),
                static_cast<unsigned>(ESP.getFreePsram()));

  if (!wifi_is_connected()) {
    set_voice_pipeline_stage("idle");
    response_status = 503;
    response_json = "WiFi disconnected";
    Serial.println(response_json);
    return false;
  }
  if (audio_bytes.empty()) {
    set_voice_pipeline_stage("idle");
    response_status = 400;
    response_json = "Missing audio payload";
    Serial.println(response_json);
    return false;
  }
  if (audio_bytes.size() > kSafeAsrAudioBytes) {
    set_voice_pipeline_stage("idle");
    response_status = 413;
    response_json = "Audio too large. Keep voice clips under 5 seconds.";
    Serial.println(response_json);
    return false;
  }
  if (ESP.getMaxAllocHeap() < (audio_bytes.size() * 3)) {
    set_voice_pipeline_stage("idle");
    response_status = 503;
    response_json = "Device memory is temporarily low. Please retry with a shorter clip.";
    Serial.println(response_json);
    return false;
  }

  display_set_status(DisplayStatus::Ready, "voice input");
  set_voice_pipeline_stage("recognizing");

  String transcript;
  String error_message;
  if (!asr_transcribe_audio_bytes(mime_type, audio_bytes, transcript, &error_message)) {
    set_voice_pipeline_stage("idle");
    response_status = 502;
    response_json = String("ASR failed: ") + error_message;
    Serial.println(response_json);
    return false;
  }

  set_voice_pipeline_stage("thinking");
  std::vector<ChatMessage> messages = g_voice_conversation_history;
  messages.push_back({"user", transcript});
  String reply_text;
  if (!chat_complete_with_messages(device_profile_build_system_prompt(), messages,
                                   reply_text, &error_message)) {
    set_voice_pipeline_stage("idle");
    response_status = 502;
    response_json = String("Chat failed: ") + error_message;
    Serial.println(response_json);
    return false;
  }

  const bool melody_was_enabled = g_melody_loop_enabled;
  g_melody_loop_enabled = false;
  display_set_status(DisplayStatus::Playing, "voice reply");
  set_voice_pipeline_stage("speaking");
  const bool tts_ok = tts_stream_text(reply_text, &error_message);
  g_melody_loop_enabled = melody_was_enabled;

  response_status = tts_ok ? 200 : 502;
  response_json = voice_chat_json_response(transcript, reply_text, tts_ok, error_message);
  if (!tts_ok) {
    Serial.println(String("TTS failed: ") + error_message);
  }
  if (tts_ok) {
    g_voice_conversation_history.push_back({"user", transcript});
    g_voice_conversation_history.push_back({"assistant", reply_text});
    trim_voice_history();
  }
  display_set_status(tts_ok ? DisplayStatus::Ready : DisplayStatus::Error,
                     tts_ok ? "voice complete" : "tts failed");
  set_voice_pipeline_stage("idle");
  return tts_ok;
}

bool execute_line(String line) {
  line.trim();
  if (line.isEmpty()) {
    return false;
  }

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
        clear_voice_history();
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
  } else if (line.equalsIgnoreCase("web")) {
    print_web_url();
    Serial.println("Ready");
  } else {
    Serial.println("Unknown command");
    print_help();
    Serial.println("Ready");
    return false;
  }

  return true;
}

void handle_http_root() {
  g_http_server.send(200, "text/html; charset=utf-8", kControlPage);
}

void handle_http_status() {
  g_http_server.send(200, "application/json; charset=utf-8", build_status_json());
}

void handle_http_command() {
  String command = request_value("text");
  command.trim();
  if (command.isEmpty()) {
    g_http_server.send(400, "text/plain; charset=utf-8", "Missing text");
    return;
  }
  execute_line(command);
  g_http_server.send(200, "text/plain; charset=utf-8", String("Executed: ") + command);
}

void handle_http_volume() {
  const String value = request_value("value");
  if (value.isEmpty()) {
    g_http_server.send(400, "text/plain; charset=utf-8", "Missing volume value");
    return;
  }
  execute_line(String("volume ") + value);
  g_http_server.send(200, "text/plain; charset=utf-8", String("Volume set: ") + value);
}

void handle_http_accompaniment() {
  String enabled = request_value("enabled");
  enabled.trim();
  enabled.toLowerCase();
  if (enabled != "on" && enabled != "off") {
    g_http_server.send(400, "text/plain; charset=utf-8", "Use enabled=on/off");
    return;
  }
  execute_line(String("accomp ") + enabled);
  g_http_server.send(200, "text/plain; charset=utf-8",
                     String("Musicbox accompaniment ") + enabled);
}

void handle_http_melody_play() {
  String song = request_value("song");
  String instrument = request_value("instrument");
  song.trim();
  instrument.trim();
  if (song.isEmpty() || instrument.isEmpty()) {
    g_http_server.send(400, "text/plain; charset=utf-8",
                       "Missing song or instrument");
    return;
  }
  execute_line(String("melody ") + song + " " + instrument);
  g_http_server.send(200, "text/plain; charset=utf-8",
                     String("Melody queued: ") + song + " " + instrument);
}

void handle_http_melody_stop() {
  execute_line("melody stop");
  g_http_server.send(200, "text/plain; charset=utf-8", "Melody stop requested");
}

void handle_http_tts() {
  String text = request_value("text");
  text.trim();
  if (text.isEmpty()) {
    g_http_server.send(400, "text/plain; charset=utf-8", "Missing text");
    return;
  }
  execute_line(String("say ") + text);
  g_http_server.send(200, "text/plain; charset=utf-8", "TTS request sent");
}

void handle_http_chat() {
  String text = request_value("text");
  text.trim();
  if (text.isEmpty()) {
    g_http_server.send(400, "text/plain; charset=utf-8", "Missing text");
    return;
  }
  execute_line(String("ask ") + text);
  g_http_server.send(200, "text/plain; charset=utf-8", "Chat request sent");
}

void handle_http_asr_chat() {
  set_voice_pipeline_stage("uploading");
  String response_json;
  int response_status = 500;
  run_voice_chat(g_uploaded_voice_mime_type, g_uploaded_voice_audio, response_json,
                 response_status);
  const String content_type =
      response_status >= 400 ? "text/plain; charset=utf-8" : "application/json; charset=utf-8";
  g_http_server.send(response_status, content_type, response_json);
  release_uploaded_voice_audio();
  g_uploaded_voice_mime_type = "audio/webm";
  g_uploaded_voice_too_large = false;
}

void handle_http_asr_chat_upload() {
  HTTPUpload& upload = g_http_server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    release_uploaded_voice_audio();
    g_uploaded_voice_audio.reserve(24 * 1024);
    g_uploaded_voice_mime_type = infer_upload_mime_type(upload);
    g_uploaded_voice_too_large = false;
    Serial.printf("ASR upload start: file=%s mime=%s\n",
                  upload.filename.c_str(),
                  g_uploaded_voice_mime_type.c_str());
    return;
  }

  if (upload.status == UPLOAD_FILE_WRITE) {
    if (g_uploaded_voice_too_large) {
      return;
    }
    if (g_uploaded_voice_audio.size() + upload.currentSize >
        AppConfig::ASR_AUDIO_DATA_URL_LIMIT) {
      g_uploaded_voice_too_large = true;
      release_uploaded_voice_audio();
      Serial.println("ASR upload rejected: payload exceeded configured limit");
      return;
    }
    g_uploaded_voice_audio.insert(g_uploaded_voice_audio.end(),
                                  upload.buf,
                                  upload.buf + upload.currentSize);
    return;
  }

  if (upload.status == UPLOAD_FILE_END && g_uploaded_voice_too_large) {
    release_uploaded_voice_audio();
  }

  if (upload.status == UPLOAD_FILE_ABORTED) {
    release_uploaded_voice_audio();
    g_uploaded_voice_too_large = false;
  }
}

void handle_http_profile() {
  String user_name = request_value("user");
  String robot_name = request_value("robot");
  user_name.trim();
  robot_name.trim();
  if (user_name.isEmpty() || robot_name.isEmpty()) {
    g_http_server.send(400, "text/plain; charset=utf-8", "Missing user or robot");
    return;
  }
  if (!device_profile_set_names(user_name, robot_name)) {
    g_http_server.send(500, "text/plain; charset=utf-8", "Profile save failed");
    return;
  }
  Serial.println("Profile saved from web");
  print_profile();
  g_http_server.send(200, "text/plain; charset=utf-8", "Profile saved");
}

void handle_http_not_found() {
  g_http_server.send(404, "text/plain; charset=utf-8", "Not found");
}

void begin_http_server() {
  if (g_http_server_started) {
    return;
  }
  g_http_server.on("/", HTTP_GET, handle_http_root);
  g_http_server.on("/api/status", HTTP_GET, handle_http_status);
  g_http_server.on("/api/command", HTTP_POST, handle_http_command);
  g_http_server.on("/api/volume", HTTP_POST, handle_http_volume);
  g_http_server.on("/api/accompaniment", HTTP_POST, handle_http_accompaniment);
  g_http_server.on("/api/melody/play", HTTP_POST, handle_http_melody_play);
  g_http_server.on("/api/melody/stop", HTTP_POST, handle_http_melody_stop);
  g_http_server.on("/api/tts", HTTP_POST, handle_http_tts);
  g_http_server.on("/api/chat", HTTP_POST, handle_http_chat);
  g_http_server.on("/api/asr-chat", HTTP_POST, handle_http_asr_chat,
                   handle_http_asr_chat_upload);
  g_http_server.on("/api/profile", HTTP_POST, handle_http_profile);
  g_http_server.onNotFound(handle_http_not_found);
  g_http_server.begin();
  g_http_server_started = true;
  ensure_mdns();
  if (g_mdns_started) {
    Serial.printf("HTTP control ready: http://%s.local/\n", kMdnsHostName);
  }
  Serial.printf("HTTP control ready: http://%s/\n", current_ip_string().c_str());
}

void service_http_server() {
  if (!wifi_is_connected()) {
    g_mdns_started = false;
    return;
  }
  ensure_mdns();
  begin_http_server();
  g_http_server.handleClient();
}

void service_serial_during_melody_playback() {
  service_http_server();
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
  if (wifi_is_connected()) {
    begin_http_server();
  }
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
  print_web_url();
  Serial.println("Ready");
}

void loop() {
  ensure_wifi();
  service_http_server();
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
    execute_line(line);
  }

  delay(20);
}
