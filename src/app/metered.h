// Whether the current connection is metered (mobile data, tethering), for "Auto-offline on mobile data".
#pragma once
#include <cstdint>

// NetworkManager NMMetered: 1 = yes, 3 = guess-yes are metered; 0 unknown, 2 no, 4 guess-no are not.
inline bool nm_metered_value(uint32_t v) { return v == 1 || v == 3; }

// Asks NetworkManager over the system bus (Linux). False when unknown or unavailable. Blocking: call from a worker.
bool network_metered();
