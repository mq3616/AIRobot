#include "device_profile.h"

#include <Preferences.h>

namespace {

constexpr char kNamespace[] = "profile";
constexpr char kUserNameKey[] = "user_name";
constexpr char kRobotNameKey[] = "robot_name";
constexpr char kPersona[] =
    "You are a highly goofy and playful robot assistant. "
    "Your humor should feel silly, light, and a little shameless. "
    "You should still be helpful, direct, and easy to talk to.";

Preferences g_preferences;
DeviceProfile g_profile;

String normalize_name(const String& value) {
  String result = value;
  result.trim();
  return result;
}

bool save_profile(const String& user_name, const String& robot_name) {
  const String normalized_user = normalize_name(user_name);
  const String normalized_robot = normalize_name(robot_name);
  if (normalized_user.isEmpty() || normalized_robot.isEmpty()) {
    return false;
  }

  if (!g_preferences.begin(kNamespace, false)) {
    return false;
  }

  const bool user_ok = g_preferences.putString(kUserNameKey, normalized_user) > 0;
  const bool robot_ok = g_preferences.putString(kRobotNameKey, normalized_robot) > 0;
  g_preferences.end();
  if (!user_ok || !robot_ok) {
    return false;
  }

  g_profile.initialized = true;
  g_profile.user_name = normalized_user;
  g_profile.robot_name = normalized_robot;
  return true;
}

}  // namespace

bool device_profile_init() {
  g_profile = DeviceProfile{};
  if (!g_preferences.begin(kNamespace, true)) {
    return false;
  }

  g_profile.user_name = normalize_name(g_preferences.getString(kUserNameKey, ""));
  g_profile.robot_name = normalize_name(g_preferences.getString(kRobotNameKey, ""));
  g_preferences.end();
  g_profile.initialized =
      !g_profile.user_name.isEmpty() && !g_profile.robot_name.isEmpty();
  return true;
}

const DeviceProfile& device_profile_get() { return g_profile; }

bool device_profile_set_names(const String& user_name, const String& robot_name) {
  return save_profile(user_name, robot_name);
}

bool device_profile_clear() {
  if (!g_preferences.begin(kNamespace, false)) {
    return false;
  }

  g_preferences.remove(kUserNameKey);
  g_preferences.remove(kRobotNameKey);
  g_preferences.end();
  g_profile = DeviceProfile{};
  return true;
}

String device_profile_build_system_prompt() {
  String prompt = kPersona;
  if (g_profile.initialized) {
    prompt += " The human user's name is ";
    prompt += g_profile.user_name;
    prompt += ". ";
    prompt += "Your name is ";
    prompt += g_profile.robot_name;
    prompt += ". ";
    prompt += "Address the user naturally and stay in character.";
  }
  return prompt;
}
