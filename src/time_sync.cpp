#include <Arduino.h>
#include <time.h>

#include "app_config.h"

void time_sync_begin() {
  configTime(AppConfig::TIME_GMT_OFFSET_SECONDS,
             AppConfig::TIME_DAYLIGHT_OFFSET_SECONDS,
             "ntp.aliyun.com", "pool.ntp.org", "time.nist.gov");
}
