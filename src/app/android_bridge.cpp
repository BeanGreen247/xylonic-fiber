#include "android_bridge.h"
#if defined(__ANDROID__)
#include <SDL.h>
#include <jni.h>
#include <mutex>
#include "mpris.h"

namespace {
struct Bridge {
    jclass cls = nullptr;   // global ref
    jmethodID update = nullptr, appicon = nullptr, landscape = nullptr, back = nullptr, exempt = nullptr, request = nullptr, density = nullptr, decode = nullptr, rate = nullptr, volumes = nullptr, pick = nullptr, poll = nullptr;
    bool tried = false;
};
Bridge g;

// App classes are invisible to FindClass on a native thread: load through the activity's class loader.
std::mutex g_bind_mu;
bool bind(JNIEnv* env, jobject act) {
    if (g.cls) return true;   // set once, last, under the mutex
    std::lock_guard<std::mutex> lk(g_bind_mu);
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
        jclass gcls = static_cast<jclass>(env->NewGlobalRef(cls));
        g.update = env->GetStaticMethodID(gcls, "update", "(Landroid/content/Context;ZZLjava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;JJI)V");
        g.back = env->GetStaticMethodID(gcls, "back", "(Landroid/app/Activity;Z)V");
        g.exempt = env->GetStaticMethodID(gcls, "batteryExempt", "(Landroid/content/Context;)Z");
        g.request = env->GetStaticMethodID(gcls, "requestBatteryExemption", "(Landroid/app/Activity;)V");
        if (env->ExceptionCheck()) env->ExceptionClear();
        g.landscape = env->GetStaticMethodID(gcls, "setLandscape", "(Landroid/app/Activity;Z)V");
        g.appicon = env->GetStaticMethodID(gcls, "setAppIcon", "(Landroid/app/Activity;Ljava/lang/String;)V");
        g.density = env->GetStaticMethodID(gcls, "density", "(Landroid/content/Context;)F");
        g.decode = env->GetStaticMethodID(gcls, "decode", "([BI)[I");
        g.rate = env->GetStaticMethodID(gcls, "outputRate", "(Landroid/content/Context;)I");
        g.volumes = env->GetStaticMethodID(gcls, "volumes", "(Landroid/content/Context;)[Ljava/lang/String;");
        g.pick = env->GetStaticMethodID(gcls, "pickFolder", "(Landroid/app/Activity;)V");
        g.poll = env->GetStaticMethodID(gcls, "pickPoll", "()Ljava/lang/String;");
        if (env->ExceptionCheck()) env->ExceptionClear();
        g.cls = gcls;   // publish last
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

void set_landscape(bool landscape) {
    Scope j;
    if (!j.ok() || !g.landscape) return;
    j.env->CallStaticVoidMethod(g.cls, g.landscape, j.act, static_cast<jboolean>(landscape));
    if (j.env->ExceptionCheck()) j.env->ExceptionClear();
}

int output_rate() {
    Scope j;
    if (!j.ok() || !g.rate) return 48000;
    const int r = j.env->CallStaticIntMethod(g.cls, g.rate, j.act);
    if (j.env->ExceptionCheck()) { j.env->ExceptionClear(); return 48000; }
    return r;
}

float density() {
    Scope j;
    if (!j.ok() || !g.density) return 0;
    const float d = j.env->CallStaticFloatMethod(g.cls, g.density, j.act);
    if (j.env->ExceptionCheck()) { j.env->ExceptionClear(); return 0; }
    return d;
}

bool decode_image(const std::string& bytes, std::vector<uint8_t>& rgba, int& w, int& h) {
    Scope j;
    if (!j.ok() || !g.decode || bytes.empty()) return false;
    jbyteArray in = j.env->NewByteArray(static_cast<jsize>(bytes.size()));
    if (!in) { j.env->ExceptionClear(); return false; }
    j.env->SetByteArrayRegion(in, 0, static_cast<jsize>(bytes.size()), reinterpret_cast<const jbyte*>(bytes.data()));
    jintArray out = static_cast<jintArray>(j.env->CallStaticObjectMethod(g.cls, g.decode, in, static_cast<jint>(2048)));
    j.env->DeleteLocalRef(in);
    if (j.env->ExceptionCheck()) { j.env->ExceptionClear(); return false; }
    if (!out) return false;
    const jsize n = j.env->GetArrayLength(out);
    bool ok = false;
    if (n > 2) {
        std::vector<jint> px(static_cast<size_t>(n));
        j.env->GetIntArrayRegion(out, 0, n, px.data());
        w = px[0]; h = px[1];
        if (w > 0 && h > 0 && static_cast<int64_t>(w) * h == n - 2) {
            rgba.resize(static_cast<size_t>(w) * h * 4);
            for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
                const uint32_t v = static_cast<uint32_t>(px[2 + i]);   // ARGB
                rgba[i * 4] = static_cast<uint8_t>(v >> 16); rgba[i * 4 + 1] = static_cast<uint8_t>(v >> 8);
                rgba[i * 4 + 2] = static_cast<uint8_t>(v); rgba[i * 4 + 3] = static_cast<uint8_t>(v >> 24);
            }
            ok = true;
        }
    }
    j.env->DeleteLocalRef(out);
    return ok;
}

void back(bool keep_running) {
    Scope j;
    if (!j.ok() || !g.back) { SDL_Quit(); return; }
    j.env->CallStaticVoidMethod(g.cls, g.back, j.act, static_cast<jboolean>(keep_running));
    if (j.env->ExceptionCheck()) j.env->ExceptionClear();
}

void set_app_icon(const char* name) {
    Scope j;
    if (!j.ok() || !g.appicon) return;
    jstring n = j.env->NewStringUTF(name);
    j.env->CallStaticVoidMethod(g.cls, g.appicon, j.act, n);
    j.env->DeleteLocalRef(n);
    if (j.env->ExceptionCheck()) j.env->ExceptionClear();
}

std::vector<std::string> volumes() {
    std::vector<std::string> out;
    Scope j;
    if (!j.ok() || !g.volumes) return out;
    jobjectArray a = static_cast<jobjectArray>(j.env->CallStaticObjectMethod(g.cls, g.volumes, j.act));
    if (j.env->ExceptionCheck()) { j.env->ExceptionClear(); return out; }
    if (!a) return out;
    const jsize n = j.env->GetArrayLength(a);
    for (jsize i = 0; i < n; ++i) {
        jstring js = static_cast<jstring>(j.env->GetObjectArrayElement(a, i));
        const char* c = js ? j.env->GetStringUTFChars(js, nullptr) : nullptr;
        out.emplace_back(c ? c : "");
        if (c) j.env->ReleaseStringUTFChars(js, c);
        if (js) j.env->DeleteLocalRef(js);
    }
    j.env->DeleteLocalRef(a);
    return out;
}

void pick_start() {
    Scope j;
    if (!j.ok() || !g.pick) return;
    j.env->CallStaticVoidMethod(g.cls, g.pick, j.act);
    if (j.env->ExceptionCheck()) j.env->ExceptionClear();
}

std::string pick_poll() {
    Scope j;
    if (!j.ok() || !g.poll) return "!";
    jstring js = static_cast<jstring>(j.env->CallStaticObjectMethod(g.cls, g.poll));
    if (j.env->ExceptionCheck()) { j.env->ExceptionClear(); return "!"; }
    std::string r;
    if (js) { const char* c = j.env->GetStringUTFChars(js, nullptr); if (c) r = c; if (c) j.env->ReleaseStringUTFChars(js, c); j.env->DeleteLocalRef(js); }
    return r;
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
