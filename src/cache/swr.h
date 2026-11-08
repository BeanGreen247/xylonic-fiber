// Stale-while-revalidate policy: serve what is cached, refresh in the background once it is stale.
#pragma once
#include <cstdint>

namespace xf::cache {
constexpr int64_t kDiscoverTtlSeconds = 5 * 60;   // original Xylonic Discover cache lifetime
enum class Freshness { Fresh, Stale, Missing };
inline Freshness classify(bool present, int64_t stored_at, int64_t now, int64_t ttl) {
    if (!present) return Freshness::Missing;
    return (now - stored_at) < ttl ? Freshness::Fresh : Freshness::Stale;
}
// Fresh: use. Stale: use and refresh in the background. Missing: wait for the network.
inline bool use_cached(Freshness f) { return f != Freshness::Missing; }
inline bool needs_refresh(Freshness f) { return f != Freshness::Fresh; }
} // namespace xf::cache
