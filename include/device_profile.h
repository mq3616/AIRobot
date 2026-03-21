#pragma once

#include <Arduino.h>

struct DeviceProfile {
  bool initialized = false;
  String user_name;
  String robot_name;
};

bool device_profile_init();
const DeviceProfile& device_profile_get();
bool device_profile_set_names(const String& user_name, const String& robot_name);
bool device_profile_clear();
String device_profile_build_system_prompt();
