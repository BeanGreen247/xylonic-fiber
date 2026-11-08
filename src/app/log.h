// Debug logging (Settings > Advanced > Debug Logging): detailed lines appended to <data>/logs/xylonic-fiber.log while enabled.
#pragma once
#include <string>
void xf_log_configure(bool enabled, const std::string& dir);   // dir: the logs folder
bool xf_log_enabled();
const std::string& xf_log_dir();
void xf_log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
