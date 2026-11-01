// Server discovery (ipc/serverDiscovery.js): probes well-known ports on common hostnames, Tailscale peers and the local
// subnets, and reports every host that answers a Subsonic ping.
#pragma once
#include <atomic>
#include <functional>
#include <string>

struct DiscoveredServer {
    std::string url, name, via, type, version;  // via: local | lan | vpn | tailscale
};

// Blocking scan; run it on a worker thread. `found` is called from scan threads (serialise in the callback).
// Stops early when cancel becomes true.
void discover_servers(const std::function<void(const DiscoveredServer&)>& found, const std::atomic<bool>& cancel);

// Exposed for tests.
bool tcp_probe(const std::string& host, int port, int timeout_ms);
