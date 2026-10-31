// Entry point. Flags (testing): --server URL --user U --pass P, --nav home|library|queue|settings|search|album:ID,
// --autoplay, --shot out.bmp [--shot-after MS], --size WxH
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "app/app.h"

int main(int argc, char** argv) {
    AppOptions o;
    for (int i = 1; i < argc; ++i) {
        auto arg = [&](const char* name) { return !std::strcmp(argv[i], name) && i + 1 < argc; };
        if (arg("--server")) o.server = argv[++i];
        else if (arg("--user")) o.user = argv[++i];
        else if (arg("--pass")) o.pass = argv[++i];
        else if (arg("--nav")) o.nav = argv[++i];
        else if (arg("--shot")) o.shot_path = argv[++i];
        else if (arg("--shot-after")) o.shot_after_ms = std::atoi(argv[++i]);
        else if (arg("--size")) std::sscanf(argv[++i], "%dx%d", &o.width, &o.height);
        else if (!std::strcmp(argv[i], "--autoplay")) o.autoplay = true;
        else if (!std::strcmp(argv[i], "--selftest")) o.selftest = true;
        else if (!std::strcmp(argv[i], "--offline")) o.offline = true;
        else if (arg("--download-album")) o.download_album = argv[++i];
    }
    App app(o);
    if (!app.init()) return 1;
    return o.selftest ? 0 : app.run();
}
