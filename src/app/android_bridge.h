// Android-only calls into org.xylonic.fiber.MediaBridge over JNI (the Java side lives in android/app/src/main/java).
#pragma once
#if defined(__ANDROID__)
#include <string>
struct MprisState;

namespace android_bridge {
// Playback + download state for the foreground service and the media session. UI thread.
void media_update(const MprisState& st);
// Back button at the top level: move to the background while music plays, otherwise close the activity.
void back(bool keep_running);
bool battery_exempt();
void request_battery_exemption();
}  // namespace android_bridge
#endif
