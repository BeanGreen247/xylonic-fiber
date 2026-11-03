#include "log.h"
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <sys/stat.h>

namespace {
std::mutex g_m;
bool g_on = false;
std::string g_dir;
}

void xf_log_configure(bool enabled, const std::string& dir) {
    std::lock_guard<std::mutex> lk(g_m);
    g_on = enabled; g_dir = dir;
    if (enabled && !dir.empty()) ::mkdir(dir.c_str(), 0755);
}
bool xf_log_enabled() { std::lock_guard<std::mutex> lk(g_m); return g_on; }
const std::string& xf_log_dir() { return g_dir; }

void xf_log(const char* fmt, ...) {
    std::lock_guard<std::mutex> lk(g_m);
    if (!g_on || g_dir.empty()) return;
    const std::string path = g_dir + "/xylonic-fiber.log";
    struct stat sb{};
    if (::stat(path.c_str(), &sb) == 0 && sb.st_size > (2 << 20)) std::rename(path.c_str(), (path + ".1").c_str());   // keep one rotated file
    std::FILE* f = std::fopen(path.c_str(), "a");
    if (!f) return;
    const std::time_t t = std::time(nullptr); char ts[32]; std::strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", std::localtime(&t));
    std::fprintf(f, "%s ", ts);
    va_list ap; va_start(ap, fmt); std::vfprintf(f, fmt, ap); va_end(ap);
    std::fputc('\n', f); std::fclose(f);
}
