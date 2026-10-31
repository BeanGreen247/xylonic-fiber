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
#else
bool secret_available() { return false; }
bool secret_store(const std::string&, const std::string&) { return false; }
bool secret_lookup(const std::string&, std::string&) { return false; }
bool secret_clear(const std::string&) { return false; }
#endif
