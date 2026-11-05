// Android-only calls into org.xylonic.fiber.MediaBridge over JNI (the Java side lives in android/app/src/main/java).
#pragma once
#if defined(__ANDROID__)
#include <cstdint>
#include <string>
#include <vector>
struct MprisState;

namespace android_bridge {
// Playback + download state for the foreground service and the media session. UI thread.
void media_update(const MprisState& st);
// Back button at the top level: move to the background while music plays, otherwise close the activity.
void back(bool keep_running);
// Display density as a WebView would report it (1.0 = 160 dpi); 0 when unknown.
float density();
// The device output sample rate (48000 when unknown).
int output_rate();
// Decodes JPEG/PNG/WebP/... through the platform; RGBA out. False when the bytes are not an image.
bool decode_image(const std::string& bytes, std::vector<uint8_t>& rgba, int& w, int& h);
bool battery_exempt();
void request_battery_exemption();
}  // namespace android_bridge
#endif
