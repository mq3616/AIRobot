#include "tts.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <algorithm>
#include <cstdlib>
#include <vector>

#include "app_config.h"
#include "audio.h"

namespace {

bool is_allowed_tts_char(uint32_t codepoint) {
  if (codepoint == '\n' || codepoint == '\r' || codepoint == '\t') {
    return true;
  }
  if (codepoint >= 0x20 && codepoint <= 0x7E) {
    return true;
  }
  if (codepoint >= 0x3000 && codepoint <= 0x303F) {
    return true;
  }
  if (codepoint >= 0x4E00 && codepoint <= 0x9FFF) {
    return true;
  }
  if (codepoint >= 0xFF00 && codepoint <= 0xFFEF) {
    return true;
  }
  return false;
}

size_t utf8_codepoint_length(uint8_t first_byte) {
  if ((first_byte & 0x80U) == 0) {
    return 1;
  }
  if ((first_byte & 0xE0U) == 0xC0U) {
    return 2;
  }
  if ((first_byte & 0xF0U) == 0xE0U) {
    return 3;
  }
  if ((first_byte & 0xF8U) == 0xF0U) {
    return 4;
  }
  return 1;
}

uint32_t decode_utf8_codepoint(const String& text, size_t index, size_t byte_count) {
  const uint8_t first = static_cast<uint8_t>(text[index]);
  if (byte_count == 1) {
    return first;
  }
  if (byte_count == 2 && index + 1 < text.length()) {
    return ((first & 0x1FU) << 6) |
           (static_cast<uint8_t>(text[index + 1]) & 0x3FU);
  }
  if (byte_count == 3 && index + 2 < text.length()) {
    return ((first & 0x0FU) << 12) |
           ((static_cast<uint8_t>(text[index + 1]) & 0x3FU) << 6) |
           (static_cast<uint8_t>(text[index + 2]) & 0x3FU);
  }
  if (byte_count == 4 && index + 3 < text.length()) {
    return ((first & 0x07U) << 18) |
           ((static_cast<uint8_t>(text[index + 1]) & 0x3FU) << 12) |
           ((static_cast<uint8_t>(text[index + 2]) & 0x3FU) << 6) |
           (static_cast<uint8_t>(text[index + 3]) & 0x3FU);
  }
  return first;
}

String sanitize_tts_text(const String& input) {
  String sanitized;
  sanitized.reserve(input.length());

  bool previous_was_space = false;
  for (size_t i = 0; i < input.length();) {
    const uint8_t first = static_cast<uint8_t>(input[i]);
    const size_t byte_count = utf8_codepoint_length(first);
    const uint32_t codepoint = decode_utf8_codepoint(input, i, byte_count);

    bool keep = is_allowed_tts_char(codepoint);
    bool as_space = false;
    if (codepoint == '*' || codepoint == '`' || codepoint == '#') {
      keep = false;
    }
    if (codepoint == '\n' || codepoint == '\r' || codepoint == '\t') {
      keep = false;
      as_space = true;
    }
    if (!keep && codepoint > 0x7F) {
      as_space = true;
    }

    if (keep) {
      const bool is_space = (codepoint == ' ');
      if (!is_space || !previous_was_space) {
        sanitized += input.substring(i, i + byte_count);
      }
      previous_was_space = is_space;
    } else if (as_space && !previous_was_space) {
      sanitized += ' ';
      previous_was_space = true;
    }

    i += byte_count;
  }

  sanitized.trim();
  while (sanitized.indexOf("  ") >= 0) {
    sanitized.replace("  ", " ");
  }

  return sanitized;
}

String truncate_utf8_at_boundary(const String& input, size_t max_bytes) {
  if (input.length() <= max_bytes) {
    return input;
  }

  size_t safe_length = 0;
  for (size_t i = 0; i < input.length();) {
    const size_t byte_count =
        utf8_codepoint_length(static_cast<uint8_t>(input[i]));
    if (i + byte_count > input.length() || safe_length + byte_count > max_bytes) {
      break;
    }
    safe_length += byte_count;
    i += byte_count;
  }

  return input.substring(0, safe_length);
}

struct WavStreamState {
  bool stream_started = false;
  bool data_size_unknown = false;
  uint16_t audio_format = 0;
  uint16_t channels = 0;
  uint16_t bits_per_sample = 0;
  uint32_t sample_rate = 0;
  size_t data_offset = 0;
  size_t data_bytes_remaining = 0;
};

struct ParsedUrl {
  String host;
  bool use_tls = true;
  uint16_t port = 443;
  String path;
};

String escape_json_string(const String& value) {
  String escaped;
  escaped.reserve(value.length() + 16);

  for (size_t i = 0; i < value.length(); ++i) {
    const char ch = value[i];
    switch (ch) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\b':
        escaped += "\\b";
        break;
      case '\f':
        escaped += "\\f";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        if (static_cast<uint8_t>(ch) < 0x20) {
          char buffer[7];
          snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned>(ch));
          escaped += buffer;
        } else {
          escaped += ch;
        }
        break;
    }
  }

  return escaped;
}

void assign_error(String* error_message, const String& message) {
  if (error_message != nullptr) {
    *error_message = message;
  }
}

bool read_http_headers(Client& client, int& status_code, bool& is_chunked,
                       size_t& content_length, String* error_message) {
  String status_line;
  do {
    status_line = client.readStringUntil('\n');
    status_line.trim();
  } while (status_line.isEmpty() && client.connected());

  Serial.print("TTS raw status line: ");
  Serial.println(status_line);

  if (!status_line.startsWith("HTTP/1.1 ") && !status_line.startsWith("HTTP/1.0 ")) {
    assign_error(error_message, String("invalid HTTP status line: ") + status_line);
    return false;
  }

  status_code = status_line.substring(status_line.indexOf(' ') + 1).toInt();
  is_chunked = false;
  content_length = 0;

  while (client.connected()) {
    String header_line = client.readStringUntil('\n');
    header_line.trim();
    if (header_line.isEmpty()) {
      return true;
    }

    const int colon_index = header_line.indexOf(':');
    if (colon_index <= 0) {
      continue;
    }

    String header_name = header_line.substring(0, colon_index);
    String header_value = header_line.substring(colon_index + 1);
    header_name.trim();
    header_value.trim();
    header_name.toLowerCase();
    header_value.toLowerCase();

    if (header_name == "transfer-encoding" && header_value.indexOf("chunked") >= 0) {
      is_chunked = true;
    } else if (header_name == "content-length") {
      content_length = static_cast<size_t>(header_value.toInt());
    }
  }

  assign_error(error_message, "HTTP headers truncated");
  return false;
}

enum class WavHeaderParseResult {
  NeedMoreData,
  Ready,
  Failed,
};

WavHeaderParseResult try_parse_wav_header(const std::vector<uint8_t>& header_bytes,
                                          size_t http_body_length, WavStreamState& state) {
  if (header_bytes.size() < 12) {
    return WavHeaderParseResult::NeedMoreData;
  }

  if (memcmp(header_bytes.data(), "RIFF", 4) != 0 ||
      memcmp(header_bytes.data() + 8, "WAVE", 4) != 0) {
    Serial.println("TTS WAV parse failed: invalid RIFF/WAVE header");
    return WavHeaderParseResult::Failed;
  }

  bool fmt_found = false;
  size_t offset = 12;
  while (offset + 8 <= header_bytes.size()) {
    const uint8_t* chunk = header_bytes.data() + offset;
    const uint32_t chunk_size = static_cast<uint32_t>(chunk[4]) |
                                (static_cast<uint32_t>(chunk[5]) << 8) |
                                (static_cast<uint32_t>(chunk[6]) << 16) |
                                (static_cast<uint32_t>(chunk[7]) << 24);
    const size_t data_offset = offset + 8;
    const size_t padded_chunk_size = chunk_size + (chunk_size % 2U);

    if (memcmp(chunk, "fmt ", 4) == 0) {
      if (header_bytes.size() < data_offset + 16) {
        return WavHeaderParseResult::NeedMoreData;
      }

      state.audio_format = static_cast<uint16_t>(header_bytes[data_offset]) |
                           (static_cast<uint16_t>(header_bytes[data_offset + 1]) << 8);
      state.channels = static_cast<uint16_t>(header_bytes[data_offset + 2]) |
                       (static_cast<uint16_t>(header_bytes[data_offset + 3]) << 8);
      state.sample_rate = static_cast<uint32_t>(header_bytes[data_offset + 4]) |
                          (static_cast<uint32_t>(header_bytes[data_offset + 5]) << 8) |
                          (static_cast<uint32_t>(header_bytes[data_offset + 6]) << 16) |
                          (static_cast<uint32_t>(header_bytes[data_offset + 7]) << 24);
      state.bits_per_sample =
          static_cast<uint16_t>(header_bytes[data_offset + 14]) |
          (static_cast<uint16_t>(header_bytes[data_offset + 15]) << 8);
      fmt_found = true;
    } else if (memcmp(chunk, "data", 4) == 0) {
      if (!fmt_found) {
        Serial.println("TTS WAV parse failed: data chunk arrived before fmt");
        return WavHeaderParseResult::Failed;
      }

      state.data_offset = data_offset;
      const size_t max_possible_payload =
          http_body_length > state.data_offset ? (http_body_length - state.data_offset) : 0;
      state.data_size_unknown = (chunk_size > max_possible_payload);
      state.data_bytes_remaining =
          state.data_size_unknown ? max_possible_payload : static_cast<size_t>(chunk_size);
      return WavHeaderParseResult::Ready;
    }

    if (header_bytes.size() < data_offset + padded_chunk_size) {
      return WavHeaderParseResult::NeedMoreData;
    }

    offset = data_offset + padded_chunk_size;
  }

  return WavHeaderParseResult::NeedMoreData;
}

bool flush_pcm_bytes(std::vector<uint8_t>& pending_pcm, WavStreamState& state,
                     size_t http_body_length, String* error_message) {
  if (!state.stream_started) {
    const WavHeaderParseResult header_result =
        try_parse_wav_header(pending_pcm, http_body_length, state);
    if (header_result == WavHeaderParseResult::Failed) {
      assign_error(error_message, "unsupported WAV header");
      return false;
    }
    if (header_result == WavHeaderParseResult::NeedMoreData) {
      if (pending_pcm.size() > AppConfig::TTS_HEADER_BUFFER_LIMIT) {
        assign_error(error_message, "WAV header exceeded buffer limit");
        return false;
      }
      return true;
    }

    if (state.audio_format != 1 || state.channels != 1 || state.bits_per_sample != 16) {
      assign_error(error_message,
                   "TTS WAV must be PCM16 mono to stream directly on device");
      return false;
    }
    if (!audio_begin_stream(state.sample_rate)) {
      assign_error(error_message, "unable to start audio output stream");
      return false;
    }

    state.stream_started = true;
    pending_pcm.erase(pending_pcm.begin(), pending_pcm.begin() + state.data_offset);
    Serial.printf("TTS playback started: %u Hz, remaining=%u bytes%s\n",
                  static_cast<unsigned>(state.sample_rate),
                  static_cast<unsigned>(state.data_bytes_remaining),
                  state.data_size_unknown ? " (http-sized)" : "");
  }

  size_t writable_bytes = pending_pcm.size();
  if (state.data_bytes_remaining < writable_bytes) {
    writable_bytes = state.data_bytes_remaining;
  }

  const size_t even_writable_bytes = writable_bytes & ~static_cast<size_t>(1);
  if (even_writable_bytes > 0) {
    if (!audio_write_pcm_16_mono_bytes(pending_pcm.data(), even_writable_bytes)) {
      assign_error(error_message, "audio stream write failed");
      return false;
    }

    pending_pcm.erase(pending_pcm.begin(), pending_pcm.begin() + even_writable_bytes);
    state.data_bytes_remaining -= even_writable_bytes;
  }

  if (state.data_bytes_remaining == 0 && !pending_pcm.empty()) {
    pending_pcm.clear();
  }

  return true;
}

bool process_body_chunk(const uint8_t* data, size_t length, std::vector<uint8_t>& pending_pcm,
                        WavStreamState& state, size_t http_body_length,
                        String* error_message) {
  if (data == nullptr || length == 0) {
    return true;
  }

  pending_pcm.insert(pending_pcm.end(), data, data + length);
  return flush_pcm_bytes(pending_pcm, state, http_body_length, error_message);
}

bool stream_wav_body(Client& client, bool is_chunked, size_t content_length,
                     String* error_message) {
  WavStreamState wav_state;
  std::vector<uint8_t> pending_pcm;
  pending_pcm.reserve(AppConfig::TTS_HEADER_BUFFER_LIMIT);

  auto finalize_stream = [&]() {
    if (wav_state.stream_started) {
      audio_end_stream();
    }
  };

  uint8_t buffer[512];
  bool ok = true;

  if (is_chunked) {
    while (true) {
      String chunk_line = client.readStringUntil('\n');
      chunk_line.trim();
      const int semicolon_index = chunk_line.indexOf(';');
      if (semicolon_index >= 0) {
        chunk_line = chunk_line.substring(0, semicolon_index);
      }

      const size_t chunk_size = static_cast<size_t>(strtoul(chunk_line.c_str(), nullptr, 16));
      if (chunk_size == 0) {
        while (client.connected()) {
          String trailer = client.readStringUntil('\n');
          trailer.trim();
          if (trailer.isEmpty()) {
            break;
          }
        }
        break;
      }

      size_t remaining = chunk_size;
      while (remaining > 0) {
        const size_t to_read = std::min(sizeof(buffer), remaining);
        const size_t bytes_read = client.readBytes(buffer, to_read);
        if (bytes_read == 0) {
          assign_error(error_message, "chunked body read timed out");
          ok = false;
          break;
        }
        if (!process_body_chunk(buffer, bytes_read, pending_pcm, wav_state, content_length,
                                error_message)) {
          ok = false;
          break;
        }
        remaining -= bytes_read;
      }
      if (!ok) {
        break;
      }

      char crlf[2] = {0, 0};
      if (client.readBytes(crlf, sizeof(crlf)) != sizeof(crlf)) {
        assign_error(error_message, "chunk trailer missing");
        ok = false;
        break;
      }
    }
  } else {
    size_t remaining = content_length;
    while (remaining > 0) {
      const size_t to_read = std::min(sizeof(buffer), remaining);
      const size_t bytes_read = client.readBytes(buffer, to_read);
      if (bytes_read == 0) {
        assign_error(error_message, "response body read timed out");
        ok = false;
        break;
      }
      if (!process_body_chunk(buffer, bytes_read, pending_pcm, wav_state, content_length,
                              error_message)) {
        ok = false;
        break;
      }
      remaining -= bytes_read;
    }
  }

  if (ok && !wav_state.stream_started) {
    assign_error(error_message, "response did not contain a playable WAV stream");
    ok = false;
  }
  if (ok && !wav_state.data_size_unknown &&
      (!pending_pcm.empty() || wav_state.data_bytes_remaining != 0)) {
    assign_error(error_message, "WAV payload ended early");
    ok = false;
  }

  finalize_stream();
  return ok;
}

String decode_json_string_fragment(const String& value) {
  String decoded;
  decoded.reserve(value.length());

  for (size_t i = 0; i < value.length(); ++i) {
    char ch = value[i];
    if (ch != '\\' || i + 1 >= value.length()) {
      decoded += ch;
      continue;
    }

    const char next = value[++i];
    switch (next) {
      case '"':
      case '\\':
      case '/':
        decoded += next;
        break;
      case 'b':
        decoded += '\b';
        break;
      case 'f':
        decoded += '\f';
        break;
      case 'n':
        decoded += '\n';
        break;
      case 'r':
        decoded += '\r';
        break;
      case 't':
        decoded += '\t';
        break;
      case 'u':
        if (i + 4 < value.length()) {
          i += 4;
        }
        break;
      default:
        decoded += next;
        break;
    }
  }

  return decoded;
}

bool extract_json_string_value(const String& json, const char* key, String& value) {
  const String token = String("\"") + key + "\":";
  const int key_index = json.indexOf(token);
  if (key_index < 0) {
    return false;
  }

  int quote_index = json.indexOf('"', key_index + token.length());
  if (quote_index < 0) {
    return false;
  }

  String raw_value;
  bool escaped = false;
  for (int i = quote_index + 1; i < json.length(); ++i) {
    const char ch = json[i];
    if (!escaped && ch == '"') {
      value = decode_json_string_fragment(raw_value);
      return true;
    }
    if (ch == '\\' && !escaped) {
      escaped = true;
      raw_value += ch;
      continue;
    }

    escaped = false;
    raw_value += ch;
  }

  return false;
}

bool parse_audio_url(const String& url, ParsedUrl& parsed_url) {
  int host_start = 0;
  if (url.startsWith("https://")) {
    parsed_url.use_tls = true;
    parsed_url.port = 443;
    host_start = 8;
  } else if (url.startsWith("http://")) {
    parsed_url.use_tls = false;
    parsed_url.port = 80;
    host_start = 7;
  } else {
    return false;
  }

  const int path_start = url.indexOf('/', host_start);
  String host_and_port = path_start >= 0 ? url.substring(host_start, path_start)
                                         : url.substring(host_start);
  parsed_url.path = path_start >= 0 ? url.substring(path_start) : "/";

  const int colon_index = host_and_port.indexOf(':');
  if (colon_index >= 0) {
    parsed_url.host = host_and_port.substring(0, colon_index);
    parsed_url.port = static_cast<uint16_t>(host_and_port.substring(colon_index + 1).toInt());
  } else {
    parsed_url.host = host_and_port;
  }

  parsed_url.host.trim();
  return !parsed_url.host.isEmpty() && !parsed_url.path.isEmpty();
}

bool request_bailian_tts_audio_url(const String& text, String& audio_url,
                                   String* error_message) {
  const String payload =
      String("{\"model\":\"") + AppConfig::BAILIAN_TTS_MODEL +
      "\",\"input\":{\"text\":\"" + escape_json_string(text) +
      "\",\"voice\":\"" + AppConfig::BAILIAN_TTS_VOICE +
      "\",\"language_type\":\"" + AppConfig::BAILIAN_TTS_LANGUAGE + "\"}}";

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(AppConfig::HTTPS_RESPONSE_TIMEOUT_MS);

  if (!client.connect(AppConfig::BAILIAN_API_HOST, AppConfig::BAILIAN_API_PORT,
                      AppConfig::HTTPS_CONNECT_TIMEOUT_MS)) {
    assign_error(error_message, "Bailian HTTPS connect failed");
    return false;
  }

  client.printf("POST %s HTTP/1.1\r\n", AppConfig::BAILIAN_TTS_PATH);
  client.printf("Host: %s\r\n", AppConfig::BAILIAN_API_HOST);
  client.println("User-Agent: AIRobot/1.0");
  client.println("Accept: application/json");
  client.println("Accept-Encoding: identity");
  client.println("Connection: close");
  client.println("Content-Type: application/json; charset=utf-8");
  client.printf("Authorization: Bearer %s\r\n", AppConfig::CLOUD_API_KEY_VALUE);
  client.printf("Content-Length: %u\r\n\r\n", static_cast<unsigned>(payload.length()));
  client.print(payload);

  int status_code = 0;
  bool is_chunked = false;
  size_t content_length = 0;
  if (!read_http_headers(client, status_code, is_chunked, content_length, error_message)) {
    client.stop();
    return false;
  }

  String response_body;
  if (is_chunked) {
    while (true) {
      String chunk_line = client.readStringUntil('\n');
      chunk_line.trim();
      const size_t chunk_size = static_cast<size_t>(strtoul(chunk_line.c_str(), nullptr, 16));
      if (chunk_size == 0) {
        break;
      }

      std::vector<char> buffer(chunk_size + 1, '\0');
      const size_t bytes_read = client.readBytes(buffer.data(), chunk_size);
      response_body += String(buffer.data()).substring(0, bytes_read);
      client.readStringUntil('\n');

      if (response_body.length() > AppConfig::TTS_JSON_BUFFER_LIMIT) {
        assign_error(error_message, "Bailian JSON response too large");
        client.stop();
        return false;
      }
    }
  } else {
    while (client.available() || client.connected()) {
      const String part = client.readString();
      if (part.isEmpty()) {
        break;
      }
      response_body += part;
      if (response_body.length() > AppConfig::TTS_JSON_BUFFER_LIMIT) {
        assign_error(error_message, "Bailian JSON response too large");
        client.stop();
        return false;
      }
    }
  }
  client.stop();

  response_body.trim();
  if (status_code != 200) {
    assign_error(error_message, String("Bailian HTTP ") + status_code + ": " + response_body);
    return false;
  }

  if (!extract_json_string_value(response_body, "url", audio_url) || audio_url.isEmpty()) {
    assign_error(error_message, String("Bailian response missing audio url: ") + response_body);
    return false;
  }

  return true;
}

bool stream_wav_from_url(const String& url, String* error_message) {
  ParsedUrl parsed_url;
  if (!parse_audio_url(url, parsed_url)) {
    assign_error(error_message, "unsupported audio url");
    return false;
  }
  int status_code = 0;
  bool is_chunked = false;
  size_t content_length = 0;

  if (parsed_url.use_tls) {
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(AppConfig::HTTPS_RESPONSE_TIMEOUT_MS);

    if (!client.connect(parsed_url.host.c_str(), parsed_url.port,
                        AppConfig::HTTPS_CONNECT_TIMEOUT_MS)) {
      assign_error(error_message, "audio download connect failed");
      return false;
    }

    client.printf("GET %s HTTP/1.1\r\n", parsed_url.path.c_str());
    client.printf("Host: %s\r\n", parsed_url.host.c_str());
    client.println("User-Agent: AIRobot/1.0");
    client.println("Accept: audio/wav, audio/*");
    client.println("Accept-Encoding: identity");
    client.println("Connection: close");
    client.println();

    if (!read_http_headers(client, status_code, is_chunked, content_length, error_message)) {
      client.stop();
      return false;
    }

    if (status_code != 200) {
      String response_body = client.readString();
      response_body.trim();
      assign_error(error_message,
                   String("audio download HTTP ") + status_code + ": " + response_body);
      client.stop();
      return false;
    }

    if (!is_chunked && content_length == 0) {
      assign_error(error_message, "audio download returned empty body");
      client.stop();
      return false;
    }

    const bool ok = stream_wav_body(client, is_chunked, content_length, error_message);
    client.stop();
    return ok;
  }

  WiFiClient client;
  client.setTimeout(AppConfig::HTTPS_RESPONSE_TIMEOUT_MS);

  if (!client.connect(parsed_url.host.c_str(), parsed_url.port,
                      AppConfig::HTTPS_CONNECT_TIMEOUT_MS)) {
    assign_error(error_message, "audio download connect failed");
    return false;
  }

  client.printf("GET %s HTTP/1.1\r\n", parsed_url.path.c_str());
  client.printf("Host: %s\r\n", parsed_url.host.c_str());
  client.println("User-Agent: AIRobot/1.0");
  client.println("Accept: audio/wav, audio/*");
  client.println("Accept-Encoding: identity");
  client.println("Connection: close");
  client.println();

  if (!read_http_headers(client, status_code, is_chunked, content_length, error_message)) {
    client.stop();
    return false;
  }

  if (status_code != 200) {
    String response_body = client.readString();
    response_body.trim();
    assign_error(error_message,
                 String("audio download HTTP ") + status_code + ": " + response_body);
    client.stop();
    return false;
  }

  if (!is_chunked && content_length == 0) {
    assign_error(error_message, "audio download returned empty body");
    client.stop();
    return false;
  }

  const bool ok = stream_wav_body(client, is_chunked, content_length, error_message);
  client.stop();
  return ok;
}

}  // namespace

bool tts_is_configured() {
  return strlen(AppConfig::CLOUD_API_KEY_VALUE) > 0;
}

bool tts_stream_text(const String& text, String* error_message) {
  String cleaned = sanitize_tts_text(text);
  if (cleaned.isEmpty()) {
    assign_error(error_message, "empty TTS text");
    return false;
  }
  if (cleaned.length() > AppConfig::TTS_TEXT_LIMIT) {
    cleaned = truncate_utf8_at_boundary(cleaned, AppConfig::TTS_TEXT_LIMIT);
  }
  cleaned.replace("\r", " ");
  cleaned.replace("\n", " ");

  if (!tts_is_configured()) {
    assign_error(error_message, "cloud API key is empty");
    return false;
  }

  Serial.printf("Bailian TTS request: %u chars\n", static_cast<unsigned>(cleaned.length()));

  String audio_url;
  if (!request_bailian_tts_audio_url(cleaned, audio_url, error_message)) {
    return false;
  }

  Serial.printf("Bailian audio url received: %s\n", audio_url.c_str());
  return stream_wav_from_url(audio_url, error_message);
}
