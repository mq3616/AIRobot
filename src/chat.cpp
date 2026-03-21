#include "chat.h"

#include <WiFiClientSecure.h>

#include <cstdlib>

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

  Serial.print("Chat raw status line: ");
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

bool extract_assistant_reply(const String& json, String& assistant_reply) {
  int message_index = json.indexOf("\"message\"");
  while (message_index >= 0) {
    if (extract_json_string_value(json, "content", assistant_reply, message_index) &&
        !assistant_reply.isEmpty()) {
      return true;
    }
    message_index = json.indexOf("\"message\"", message_index + 1);
  }
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

      const size_t chunk_size = static_cast<size_t>(strtoul(chunk_line.c_str(), nullptr, 16));
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

}  // namespace

bool chat_is_configured() {
  return strlen(AppConfig::CLOUD_API_KEY_VALUE) > 0;
}

bool chat_complete_once(const String& system_prompt, const String& user_text,
                        String& assistant_reply, String* error_message) {
  assistant_reply = "";

  if (!chat_is_configured()) {
    assign_error(error_message, "cloud API key is empty");
    return false;
  }

  String system_clean = system_prompt;
  system_clean.trim();
  String user_clean = user_text;
  user_clean.trim();
  if (user_clean.isEmpty()) {
    assign_error(error_message, "empty user text");
    return false;
  }
  if (user_clean.length() > AppConfig::CHAT_TEXT_LIMIT) {
    user_clean = user_clean.substring(0, AppConfig::CHAT_TEXT_LIMIT);
  }

  const String payload =
      String("{\"model\":\"") + AppConfig::BAILIAN_CHAT_MODEL +
      "\",\"messages\":[{\"role\":\"system\",\"content\":\"" +
      escape_json_string(system_clean) + "\"},{\"role\":\"user\",\"content\":\"" +
      escape_json_string(user_clean) + "\"}]}";

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(AppConfig::HTTPS_RESPONSE_TIMEOUT_MS);

  Serial.printf("Bailian chat request: %u chars\n", static_cast<unsigned>(user_clean.length()));
  if (!client.connect(AppConfig::BAILIAN_API_HOST, AppConfig::BAILIAN_API_PORT,
                      AppConfig::HTTPS_CONNECT_TIMEOUT_MS)) {
    assign_error(error_message, "Bailian chat connect failed");
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
  if (!read_http_body(client, is_chunked, content_length, AppConfig::CHAT_JSON_BUFFER_LIMIT,
                      response_body, error_message)) {
    client.stop();
    return false;
  }
  client.stop();
  response_body.trim();

  if (status_code != 200) {
    assign_error(error_message,
                 String("Bailian chat HTTP ") + status_code + ": " + response_body);
    return false;
  }

  if (!extract_assistant_reply(response_body, assistant_reply)) {
    assign_error(error_message, String("Bailian chat parse failed: ") + response_body);
    return false;
  }

  assistant_reply.trim();
  return !assistant_reply.isEmpty();
}
