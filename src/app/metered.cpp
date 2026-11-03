#include "metered.h"
#if defined(XF_MPRIS)
#include <systemd/sd-bus.h>

bool network_metered() {
    sd_bus* bus = nullptr;
    if (sd_bus_open_system(&bus) < 0) return false;
    uint32_t value = 0;
    sd_bus_error err = SD_BUS_ERROR_NULL;
    const int r = sd_bus_get_property_trivial(bus, "org.freedesktop.NetworkManager", "/org/freedesktop/NetworkManager", "org.freedesktop.NetworkManager", "Metered", &err, 'u', &value);
    sd_bus_error_free(&err);
    sd_bus_unref(bus);
    return r >= 0 && nm_metered_value(value);
}
#else
bool network_metered() { return false; }   // Android and other platforms plug their own detection in here
#endif
