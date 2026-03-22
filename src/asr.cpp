#include "asr.h"

#include <WiFiClientSecure.h>

#include <cstdlib>
#include <vector>

#include "app_config.h"

namespace {

String escape_json_string(const String& value) {
  String escaped;
  escaped.reserve(value.length() + 32);

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

bool read_http_headers(WiFiClientSecure& client, int& status_code, bool& is_chunked,
                       size_t& content_length, String* error_message) {
  String status_line;
  do {
    status_line = client.readStringUntil('\n');
    status_line.trim();
  } while (status_line.isEmpty() && client.connected());

  Serial.print("ASR raw status line: ");
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

bool read_http_body(WiFiClientSecure& client, bool is_chunked, size_t content_length,
                    size_t max_length, String& body, String* error_message) {
  body = "";

  if (is_chunked) {
    while (true) {
      String chunk_line = client.readStringUntil('\n');
      chunk_line.trim();
      const int semicolon_index = chunk_line.indexOf(';');
      if (semicolon_index >= 0) {
        chunk_line = chunk_line.substring(0, semicolon_index);
      }

      const size_t chunk_size =
          static_cast<size_t>(strtoul(chunk_line.c_str(), nullptr, 16));
      if (chunk_size == 0) {
        while (client.connected()) {
          String trailer = client.readStringUntil('\n');
          trailer.trim();
          if (trailer.isEmpty()) {
            break;
          }
        }
        return true;
      }

      String chunk;
      chunk.reserve(chunk_size);
      while (chunk.length() < chunk_size) {
        const int ch = client.read();
        if (ch < 0) {
          assign_error(error_message, "chunked body read timed out");
          return false;
        }
        chunk += static_cast<char>(ch);
      }
      body += chunk;
      if (body.length() > max_length) {
        assign_error(error_message, "response body too large");
        return false;
      }

      char crlf[2] = {0, 0};
      if (client.readBytes(crlf, sizeof(crlf)) != sizeof(crlf)) {
        assign_error(error_message, "chunk trailer missing");
        return false;
      }
    }
  }

  size_t remaining = content_length;
  while (remaining > 0) {
    const int ch = client.read();
    if (ch < 0) {
      assign_error(error_message, "response body read timed out");
      return false;
    }
    body += static_cast<char>(ch);
    --remaining;
    if (body.length() > max_length) {
      assign_error(error_message, "response body too large");
      return false;
    }
  }

  return true;
}

String decode_json_string_fragment(const String& value) {
  String decoded;
  decoded.reserve(value.length());

  for (size_t i = 0; i < value.length(); ++i) {
    const char ch = value[i];
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

bool extract_json_string_value(const String& json, const char* key, String& value,
                               int search_start = 0) {
  const String token = String("\"") + key + "\":";
  const int key_index = json.indexOf(token, search_start);
  if (key_index < 0) {
    return false;
  }

  const int quote_index = json.indexOf('"', key_index + token.length());
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

bool extract_transcript(const String& json, String& transcript) {
  int message_index = json.indexOf("\"message\"");
  while (message_index >= 0) {
    if (extract_json_string_value(json, "content", transcript, message_index) &&
        !transcript.isEmpty()) {
      return true;
    }
    message_index = json.indexOf("\"message\"", message_index + 1);
  }
  return false;
}

String base64_encode(const uint8_t* data, size_t length) {
  static constexpr char kAlphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  String encoded;
  encoded.reserve(((length + 2) / 3) * 4);

  for (size_t i = 0; i < length; i += 3) {
    const uint32_t octet_a = data[i];
    const uint32_t octet_b = (i + 1 < length) ? data[i + 1] : 0;
    const uint32_t octet_c = (i + 2 < length) ? data[i + 2] : 0;
    const uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

    encoded += kAlphabet[(triple >> 18) & 0x3F];
    encoded += kAlphabet[(triple >> 12) & 0x3F];
    encoded += (i + 1 < length) ? kAlphabet[(triple >> 6) & 0x3F] : '=';
    encoded += (i + 2 < length) ? kAlphabet[triple & 0x3F] : '=';
  }

  return encoded;
}

}  // namespace

bool asr_is_configured() {
  return strlen(AppConfig::CLOUD_API_KEY_VALUE) > 0;
}

bool asr_transcribe_audio_bytes(const String& mime_type,
                                const std::vector<uint8_t>& audio_bytes,
                                String& transcript,
                                String* error_message) {
  transcript = "";

  String mime_clean = mime_type;
  mime_clean.trim();
  mime_clean.toLowerCase();
  if (audio_bytes.empty()) {
    assign_error(error_message, "empty audio payload");
    return false;
  }
  if (audio_bytes.size() > AppConfig::ASR_AUDIO_DATA_URL_LIMIT) {
    assign_error(error_message, "audio payload too large");
    return false;
  }
  if (mime_clean.isEmpty()) {
    mime_clean = "audio/webm";
  }

  if (!asr_is_configured()) {
    assign_error(error_message, "cloud API key is empty");
    return false;
  }

  const String audio_data_url =
      String("data:") + mime_clean + ";base64," +
      base64_encode(audio_bytes.data(), audio_bytes.size());

  const String payload =
      String("{\"model\":\"") + AppConfig::BAILIAN_ASR_MODEL +
      "\",\"messages\":[{\"role\":\"user\",\"content\":[{\"type\":\"input_audio\","
      "\"input_audio\":{\"data\":\"" +
      escape_json_string(audio_data_url) +
      "\"}}]}],\"stream\":false,\"asr_options\":{\"language\":\"zh\",\"enable_itn\":true}}";

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(AppConfig::HTTPS_RESPONSE_TIMEOUT_MS);

  Serial.printf("Bailian ASR request: mime=%s audio=%u bytes\n",
                mime_clean.c_str(),
                static_cast<unsigned>(audio_bytes.size()));
  if (!client.connect(AppConfig::BAILIAN_API_HOST, AppConfig::BAILIAN_API_PORT,
                      AppConfig::HTTPS_CONNECT_TIMEOUT_MS)) {
    assign_error(error_message, "Bailian ASR connect failed");
    return false;
  }

  client.printf("POST %s HTTP/1.1\r\n", AppConfig::BAILIAN_CHAT_PATH);
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
  if (!read_http_body(client, is_chunked, content_length, AppConfig::ASR_JSON_BUFFER_LIMIT,
                      response_body, error_message)) {
    client.stop();
    return false;
  }
  client.stop();
  response_body.trim();

  if (status_code != 200) {
    assign_error(error_message,
                 String("Bailian ASR HTTP ") + status_code + ": " + response_body);
    return false;
  }

  if (!extract_transcript(response_body, transcript)) {
    assign_error(error_message, String("Bailian ASR parse failed: ") + response_body);
    return false;
  }

  transcript.trim();
  if (transcript.isEmpty()) {
    assign_error(error_message, "empty transcript");
    return false;
  }
  return true;
}
