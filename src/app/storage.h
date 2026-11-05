// Permanent-cache location: volumes, folder picker, and moving the cache tree with progress.
#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

struct StorageVolume {
    std::string label, path;
    bool removable = false;
    uint64_t free_bytes = 0, total_bytes = 0;
};

// Mobile volumes (Android internal + SD/external via SDL; iOS has one). Empty on desktop.
std::vector<StorageVolume> storage_volumes();
// Free and total bytes of the filesystem holding path; false when unknown.
bool disk_space(const std::string& path, uint64_t& free_bytes, uint64_t& total_bytes);
// Native folder chooser (zenity/kdialog on Linux, osascript on macOS). Empty on cancel or when none is installed.
std::string pick_folder(const std::string& start);
// Total size of regular files below dir.
uint64_t tree_bytes(const std::string& dir);
// Copies src's tree into dst (merging), then removes the copied source files. progress gets bytes copied so far.
// Returns false and leaves the source intact when a copy fails; error describes the failure.
bool move_tree(const std::string& src, const std::string& dst, std::atomic<uint64_t>& progress, std::string& error);
// Re-roots a stored file path after the cache moved: if p contains marker (e.g. "/downloads/<acct>/") and does not
// already start with dir (which ends in that marker), returns dir plus the part after the marker; otherwise p.
std::string heal_path(const std::string& p, const std::string& marker, const std::string& dir);
// root with a trailing slash, or fallback when root is empty or not a usable directory.
std::string resolve_cache_root(const std::string& root, const std::string& fallback);
