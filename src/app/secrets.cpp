#include "secrets.h"
#if defined(__linux__) && !defined(__ANDROID__)
#include <dlfcn.h>
#include <cstring>

namespace {
struct SecretSchemaAttr { const char* name; int type; };
struct SecretSchema {  // mirrors libsecret's SecretSchema layout (stable ABI)
    const char* name;
    int flags;
    SecretSchemaAttr attributes[32];
    int reserved;
    void* reserved1; void* reserved2; void* reserved3; void* reserved4; void* reserved5; void* reserved6; void* reserved7;
};
using StoreFn = int (*)(const SecretSchema*, const char*, const char*, const char*, void*, void**, ...);
using LookupFn = char* (*)(const SecretSchema*, void*, void**, ...);
using ClearFn = int (*)(const SecretSchema*, void*, void**, ...);
using FreeFn = void (*)(char*);
using ErrFreeFn = void (*)(void*);

struct Lib {
    StoreFn store = nullptr;
    LookupFn lookup = nullptr;
    ClearFn clear = nullptr;
    FreeFn pfree = nullptr;
    ErrFreeFn efree = nullptr;
    SecretSchema schema{};
    Lib() {
        void* h = dlopen("libsecret-1.so.0", RTLD_LAZY | RTLD_LOCAL);
        if (!h) return;
        void* g = dlopen("libglib-2.0.so.0", RTLD_LAZY | RTLD_LOCAL);
        store = reinterpret_cast<StoreFn>(dlsym(h, "secret_password_store_sync"));
        lookup = reinterpret_cast<LookupFn>(dlsym(h, "secret_password_lookup_sync"));
        clear = reinterpret_cast<ClearFn>(dlsym(h, "secret_password_clear_sync"));
        pfree = reinterpret_cast<FreeFn>(dlsym(h, "secret_password_free"));
        efree = reinterpret_cast<ErrFreeFn>(g ? dlsym(g, "g_error_free") : nullptr);
        schema.name = "org.xylonic-fiber.Account";
        schema.flags = 0;  // SECRET_SCHEMA_NONE
        schema.attributes[0] = {"account", 0};  // SECRET_SCHEMA_ATTRIBUTE_STRING
        if (!store || !lookup || !clear || !pfree) store = nullptr;
    }
};
Lib& lib() { static Lib l; return l; }
void drop(void* err) { if (err && lib().efree) lib().efree(err); }
}  // namespace

bool secret_available() { return lib().store != nullptr; }

bool secret_store(const std::string& account, const std::string& pw) {
    Lib& l = lib();
    if (!l.store) return false;
    void* err = nullptr;
    int ok = l.store(&l.schema, "default", "xylonic-fiber account", pw.c_str(), nullptr, &err, "account", account.c_str(), static_cast<const char*>(nullptr));
    drop(err);
    return ok != 0;
}

bool secret_lookup(const std::string& account, std::string& pw) {
    Lib& l = lib();
    if (!l.store) return false;
    void* err = nullptr;
    char* r = l.lookup(&l.schema, nullptr, &err, "account", account.c_str(), static_cast<const char*>(nullptr));
    drop(err);
    if (!r) return false;
    pw = r;
    l.pfree(r);
    return true;
}

bool secret_clear(const std::string& account) {
    Lib& l = lib();
    if (!l.store) return false;
    void* err = nullptr;
    int ok = l.clear(&l.schema, nullptr, &err, "account", account.c_str(), static_cast<const char*>(nullptr));
    drop(err);
    return ok != 0;
}
#elif defined(__ANDROID__)
// Android Keystore through org.xylonic.fiber.SecretStore (android/app/src/main/java), reached over JNI.
#include <SDL.h>
#include <jni.h>

namespace {
struct Jni {
    JNIEnv* env = nullptr;
    jobject act = nullptr;
    jclass cls = nullptr;
    Jni() {
        env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());   // attaches the calling thread
        act = static_cast<jobject>(SDL_AndroidGetActivity());
        if (!env || !act) return;
        // App classes are not visible to FindClass from a native thread: go through the activity's class loader.
        jclass actc = env->GetObjectClass(act);
        jmethodID gcl = env->GetMethodID(actc, "getClassLoader", "()Ljava/lang/ClassLoader;");
        jobject loader = gcl ? env->CallObjectMethod(act, gcl) : nullptr;
        jclass lc = env->FindClass("java/lang/ClassLoader");
        jmethodID lm = lc ? env->GetMethodID(lc, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;") : nullptr;
        jstring n = env->NewStringUTF("org.xylonic.fiber.SecretStore");
        if (loader && lm) cls = static_cast<jclass>(env->CallObjectMethod(loader, lm, n));
        if (env->ExceptionCheck()) { env->ExceptionClear(); cls = nullptr; }
        env->DeleteLocalRef(n);
        if (lc) env->DeleteLocalRef(lc);
        if (loader) env->DeleteLocalRef(loader);
        env->DeleteLocalRef(actc);
    }
    ~Jni() { if (env && cls) env->DeleteLocalRef(cls); if (env && act) env->DeleteLocalRef(act); }
    bool ok() const { return env && act && cls; }
};
}  // namespace

bool secret_available() { Jni j; return j.ok(); }

bool secret_store(const std::string& account, const std::string& pw) {
    Jni j;
    if (!j.ok()) return false;
    jmethodID m = j.env->GetStaticMethodID(j.cls, "store", "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)Z");
    if (!m) { j.env->ExceptionClear(); return false; }
    jstring a = j.env->NewStringUTF(account.c_str()), p = j.env->NewStringUTF(pw.c_str());
    const bool ok = j.env->CallStaticBooleanMethod(j.cls, m, j.act, a, p) == JNI_TRUE;
    if (j.env->ExceptionCheck()) j.env->ExceptionClear();
    j.env->DeleteLocalRef(a); j.env->DeleteLocalRef(p);
    return ok;
}

bool secret_lookup(const std::string& account, std::string& pw) {
    Jni j;
    if (!j.ok()) return false;
    jmethodID m = j.env->GetStaticMethodID(j.cls, "lookup", "(Landroid/content/Context;Ljava/lang/String;)Ljava/lang/String;");
    if (!m) { j.env->ExceptionClear(); return false; }
    jstring a = j.env->NewStringUTF(account.c_str());
    jstring r = static_cast<jstring>(j.env->CallStaticObjectMethod(j.cls, m, j.act, a));
    if (j.env->ExceptionCheck()) { j.env->ExceptionClear(); r = nullptr; }
    j.env->DeleteLocalRef(a);
    if (!r) return false;
    const char* c = j.env->GetStringUTFChars(r, nullptr);
    pw = c ? c : "";
    if (c) j.env->ReleaseStringUTFChars(r, c);
    j.env->DeleteLocalRef(r);
    return true;
}

bool secret_clear(const std::string& account) {
    Jni j;
    if (!j.ok()) return false;
    jmethodID m = j.env->GetStaticMethodID(j.cls, "clear", "(Landroid/content/Context;Ljava/lang/String;)Z");
    if (!m) { j.env->ExceptionClear(); return false; }
    jstring a = j.env->NewStringUTF(account.c_str());
    const bool ok = j.env->CallStaticBooleanMethod(j.cls, m, j.act, a) == JNI_TRUE;
    if (j.env->ExceptionCheck()) j.env->ExceptionClear();
    j.env->DeleteLocalRef(a);
    return ok;
}
#elif defined(__APPLE__)
// macOS and iOS Keychain (generic password items, service "xylonic-fiber"). Plain C API: no Objective-C needed.
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

namespace {
const char* const kService = "xylonic-fiber";
CFStringRef cfstr(const std::string& s) { return CFStringCreateWithBytes(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(s.data()), static_cast<CFIndex>(s.size()), kCFStringEncodingUTF8, false); }
CFMutableDictionaryRef query(const std::string& account) {
    CFMutableDictionaryRef q = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFStringRef svc = cfstr(kService), acc = cfstr(account);
    CFDictionarySetValue(q, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(q, kSecAttrService, svc);
    CFDictionarySetValue(q, kSecAttrAccount, acc);
    CFRelease(svc); CFRelease(acc);
    return q;
}
}  // namespace

bool secret_available() { return true; }

bool secret_store(const std::string& account, const std::string& pw) {
    CFMutableDictionaryRef q = query(account);
    SecItemDelete(q);   // replace any existing item
    CFDataRef data = CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(pw.data()), static_cast<CFIndex>(pw.size()));
    CFDictionarySetValue(q, kSecValueData, data);
    CFDictionarySetValue(q, kSecAttrAccessible, kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly);
    const OSStatus st = SecItemAdd(q, nullptr);
    CFRelease(data); CFRelease(q);
    return st == errSecSuccess;
}

bool secret_lookup(const std::string& account, std::string& pw) {
    CFMutableDictionaryRef q = query(account);
    CFDictionarySetValue(q, kSecReturnData, kCFBooleanTrue);
    CFDictionarySetValue(q, kSecMatchLimit, kSecMatchLimitOne);
    CFTypeRef out = nullptr;
    const OSStatus st = SecItemCopyMatching(q, &out);
    CFRelease(q);
    if (st != errSecSuccess || !out) return false;
    CFDataRef d = static_cast<CFDataRef>(out);
    pw.assign(reinterpret_cast<const char*>(CFDataGetBytePtr(d)), static_cast<size_t>(CFDataGetLength(d)));
    CFRelease(out);
    return true;
}

bool secret_clear(const std::string& account) {
    CFMutableDictionaryRef q = query(account);
    const OSStatus st = SecItemDelete(q);
    CFRelease(q);
    return st == errSecSuccess || st == errSecItemNotFound;
}
#else
bool secret_available() { return false; }
bool secret_store(const std::string&, const std::string&) { return false; }
bool secret_lookup(const std::string&, std::string&) { return false; }
bool secret_clear(const std::string&) { return false; }
#endif
