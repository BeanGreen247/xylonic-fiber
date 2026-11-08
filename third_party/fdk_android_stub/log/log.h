/* fdk-aac includes AOSP's <log/log.h> on Android for android_errorWriteLog (security-bug logging inside the platform build). The NDK has no such
 * header, so the call becomes a no-op here. */
#pragma once
static inline int android_errorWriteLog(int tag, const char* subTag) { (void)tag; (void)subTag; return 0; }
