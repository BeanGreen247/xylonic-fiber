// Round trip through the OS keyring; skipped (exit 0) when no Secret Service is reachable.
#include <cstdio>
#include "../src/app/secrets.h"
int main() {
    if (!secret_available()) { std::printf("SKIP no libsecret\n"); return 0; }
    const std::string acct = "xylonic-fiber-test|http://example.invalid|tester";
    if (!secret_store(acct, "pä ss\"word")) { std::printf("SKIP no unlocked keyring\n"); return 0; }
    std::string out;
    int fails = 0;
    if (!secret_lookup(acct, out) || out != "pä ss\"word") { std::printf("FAIL lookup '%s'\n", out.c_str()); ++fails; }
    if (!secret_clear(acct)) { std::printf("FAIL clear\n"); ++fails; }
    if (secret_lookup(acct, out)) { std::printf("FAIL still present\n"); ++fails; }
    std::printf(fails ? "FAILED\n" : "OK\n");
    return fails ? 1 : 0;
}
