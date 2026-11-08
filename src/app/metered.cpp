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
#elif defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#include <SystemConfiguration/SystemConfiguration.h>
#include <netinet/in.h>
#include <cstring>
// iOS: the default route runs over the cellular radio (WWAN) when Wi-Fi is not carrying it. Reachability flags need no entitlement.
bool network_metered() {
    sockaddr_in zero;
    std::memset(&zero, 0, sizeof zero);
    zero.sin_len = sizeof zero;
    zero.sin_family = AF_INET;
    SCNetworkReachabilityRef ref = SCNetworkReachabilityCreateWithAddress(kCFAllocatorDefault, reinterpret_cast<const sockaddr*>(&zero));
    if (!ref) return false;
    SCNetworkReachabilityFlags flags = 0;
    const bool ok = SCNetworkReachabilityGetFlags(ref, &flags);
    CFRelease(ref);
    return ok && (flags & kSCNetworkReachabilityFlagsReachable) && (flags & kSCNetworkReachabilityFlagsIsWWAN);
}
#else
bool network_metered() { return false; }   // macOS has no metered-connection notion; tethering shows up as Wi-Fi
#endif
#elif defined(__ANDROID__)
#include <SDL.h>
#include <jni.h>
// Android: ConnectivityManager.isActiveNetworkMetered() through SDL's JNI helpers (needs ACCESS_NETWORK_STATE).
bool network_metered() {
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());   // attaches the calling thread
    jobject act = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !act) return false;
    bool metered = false;
    jclass ctxc = env->FindClass("android/content/Context");
    jmethodID gss = ctxc ? env->GetMethodID(ctxc, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;") : nullptr;
    jstring name = env->NewStringUTF("connectivity");
    jobject cm = gss ? env->CallObjectMethod(act, gss, name) : nullptr;
    if (env->ExceptionCheck()) { env->ExceptionClear(); cm = nullptr; }
    if (cm) {
        jclass cmc = env->GetObjectClass(cm);
        jmethodID im = env->GetMethodID(cmc, "isActiveNetworkMetered", "()Z");
        if (im) metered = env->CallBooleanMethod(cm, im) == JNI_TRUE;
        if (env->ExceptionCheck()) { env->ExceptionClear(); metered = false; }
        env->DeleteLocalRef(cmc);
        env->DeleteLocalRef(cm);
    }
    env->DeleteLocalRef(name);
    if (ctxc) env->DeleteLocalRef(ctxc);
    env->DeleteLocalRef(act);
    return metered;
}
#else
bool network_metered() { return false; }   // other platforms
#endif
