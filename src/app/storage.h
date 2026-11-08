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
// Moves src's tree into dst (merging). Same filesystem: renames (instant). Otherwise copies each file, verifies its size, then
// removes that source file, so an interrupted move can simply be run again. progress gets bytes moved so far.
// Returns false when a file fails or cancel is set (finished files stay moved); error describes the failure.
bool move_tree(const std::string& src, const std::string& dst, std::atomic<uint64_t>& progress, std::string& error,
               const std::atomic<bool>* cancel = nullptr);
// Android: begin the system folder chooser, then poll each frame. poll returns false while open; true when done with the
// path in out (empty on cancel). pick_folder_async_supported is false where pick_folder (blocking) is used instead.
bool pick_folder_async_supported();
void pick_folder_async_start();
bool pick_folder_async_poll(std::string& out);
// Re-roots a stored file path after the cache moved: if p contains marker (e.g. "/downloads/<acct>/") and does not
// already start with dir (which ends in that marker), returns dir plus the part after the marker; otherwise p.
std::string heal_path(const std::string& p, const std::string& marker, const std::string& dir);
// root with a trailing slash, or fallback when root is empty or not a usable directory.
std::string resolve_cache_root(const std::string& root, const std::string& fallback);
