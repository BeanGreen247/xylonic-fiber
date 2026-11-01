// OS credential storage. Linux: Secret Service through libsecret, loaded at run time (no build or runtime hard dependency).
// Other platforms get their native backend with their ports; until then these return false and the app asks for the password.
#pragma once
#include <string>

bool secret_available();
bool secret_store(const std::string& account, const std::string& password);
bool secret_lookup(const std::string& account, std::string& password);
bool secret_clear(const std::string& account);
