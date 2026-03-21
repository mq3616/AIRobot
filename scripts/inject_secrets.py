Import("env")

import os


def append_string_define(name, value):
    env.Append(CPPDEFINES=[(name, '\\"%s\\"' % value)])


for macro_name, env_name in [
    ("WIFI_SSID", "WIFI_SSID"),
    ("WIFI_PASSWORD", "WIFI_PASSWORD"),
    ("OPENAI_API_KEY", "OPENAI_API_KEY"),
]:
    value = os.environ.get(env_name)
    if value:
        append_string_define(macro_name, value)
