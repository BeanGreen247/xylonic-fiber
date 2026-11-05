#include "android_bridge.h"
#if defined(__ANDROID__)
#include <SDL.h>
#include <jni.h>
#include "mpris.h"

namespace {
struct Bridge {
    jclass cls = nullptr;   // global ref
    jmethodID update = nullptr, back = nullptr, exempt = nullptr, request = nullptr;
    bool tried = false;
};
Bridge g;

// App classes are invisible to FindClass on a native thread: load through the activity's class loader.
bool bind(JNIEnv* env, jobject act) {
    if (g.cls) return true;
    if (g.tried) return false;
    g.tried = true;
    jclass actc = env->GetObjectClass(act);
    jmethodID gcl = env->GetMethodID(actc, "getClassLoader", "()Ljava/lang/ClassLoader;");
    jobject loader = gcl ? env->CallObjectMethod(act, gcl) : nullptr;
    jclass lc = env->FindClass("java/lang/ClassLoader");
    jmethodID lm = lc ? env->GetMethodID(lc, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;") : nullptr;
    jstring n = env->NewStringUTF("org.xylonic.fiber.MediaBridge");
    jclass cls = (loader && lm) ? static_cast<jclass>(env->CallObjectMethod(loader, lm, n)) : nullptr;
    if (env->ExceptionCheck()) { env->ExceptionClear(); cls = nullptr; }
    if (cls) {
        g.cls = static_cast<jclass>(env->NewGlobalRef(cls));
        g.update = env->GetStaticMethodID(g.cls, "update", "(Landroid/content/Context;ZZLjava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;JJI)V");
        g.back = env->GetStaticMethodID(g.cls, "back", "(Landroid/app/Activity;Z)V");
        g.exempt = env->GetStaticMethodID(g.cls, "batteryExempt", "(Landroid/content/Context;)Z");
        g.request = env->GetStaticMethodID(g.cls, "requestBatteryExemption", "(Landroid/app/Activity;)V");
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(cls);
    }
    env->DeleteLocalRef(n);
    if (lc) env->DeleteLocalRef(lc);
    if (loader) env->DeleteLocalRef(loader);
    env->DeleteLocalRef(actc);
    return g.cls != nullptr;
}

struct Scope {
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());   // attaches the calling thread
    jobject act = static_cast<jobject>(SDL_AndroidGetActivity());
    bool ok() const { return env && act && bind(env, act); }
    ~Scope() { if (env && act) env->DeleteLocalRef(act); }
};
}  // namespace

namespace android_bridge {
void media_update(const MprisState& st) {
    Scope j;
    if (!j.ok() || !g.update) return;
    auto js = [&](const std::string& v) { return j.env->NewStringUTF(v.c_str()); };
    const std::string art = st.art_url.rfind("file://", 0) == 0 ? st.art_url.substr(7) : std::string();
    jstring t = js(st.title), a = js(st.artist), al = js(st.album), ap = js(art);
    j.env->CallStaticVoidMethod(g.cls, g.update, j.act, static_cast<jboolean>(st.has_song), static_cast<jboolean>(st.playing), t, a, al, ap,
                                static_cast<jlong>(st.length_s * 1000.0), static_cast<jlong>(st.position_s * 1000.0), static_cast<jint>(st.downloads));
    if (j.env->ExceptionCheck()) j.env->ExceptionClear();
    j.env->DeleteLocalRef(t); j.env->DeleteLocalRef(a); j.env->DeleteLocalRef(al); j.env->DeleteLocalRef(ap);
}

void back(bool keep_running) {
    Scope j;
    if (!j.ok() || !g.back) { SDL_Quit(); return; }
    j.env->CallStaticVoidMethod(g.cls, g.back, j.act, static_cast<jboolean>(keep_running));
    if (j.env->ExceptionCheck()) j.env->ExceptionClear();
}

bool battery_exempt() {
    Scope j;
    if (!j.ok() || !g.exempt) return true;
    const bool r = j.env->CallStaticBooleanMethod(g.cls, g.exempt, j.act) == JNI_TRUE;
    if (j.env->ExceptionCheck()) { j.env->ExceptionClear(); return true; }
    return r;
}

void request_battery_exemption() {
    Scope j;
    if (!j.ok() || !g.request) return;
    j.env->CallStaticVoidMethod(g.cls, g.request, j.act);
    if (j.env->ExceptionCheck()) j.env->ExceptionClear();
}
}  // namespace android_bridge
#endif
