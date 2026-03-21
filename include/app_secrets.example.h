#pragma once

// Optional local fallback. Prefer build_flags or PlatformIO environment variables.
// Example:
//   build_flags =
//     '-DWIFI_SSID="\"your-ssid\""'
//     '-DWIFI_PASSWORD="\"your-password\""'
//     '-DOPENAI_API_KEY="\"sk-...\""'

#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define OPENAI_API_KEY "YOUR_OPENAI_API_KEY"
