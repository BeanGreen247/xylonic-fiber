#include <cstdio>
#include "../src/app/metered.h"
int main() {
    int fails = 0;
    const bool want[5] = {false, true, false, true, false};   // NMMetered 0..4
    for (uint32_t v = 0; v < 5; ++v) if (nm_metered_value(v) != want[v]) { std::printf("FAIL value %u\n", v); ++fails; }
    if (nm_metered_value(99)) { std::printf("FAIL unknown value\n"); ++fails; }
    network_metered();   // must not crash or hang without NetworkManager
    std::printf(fails ? "FAILED\n" : "OK\n"); return fails ? 1 : 0;
}
