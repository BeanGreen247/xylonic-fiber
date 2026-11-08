// Config accounts: the same server behind a new address is recognised by its library fingerprint.
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>
#include "../src/app/config.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

static std::vector<std::string> ids(int from, int to) {
    std::vector<std::string> v;
    for (int i = from; i < to; ++i) v.push_back("ar-" + std::to_string(i * 7919));
    return v;
}

int main() {
    const auto home = library_fingerprint(ids(0, 800));
    const auto away = library_fingerprint(ids(0, 800));      // same library through another address
    const auto grown = library_fingerprint(ids(0, 830));     // it gained 30 artists meanwhile
    const auto other = library_fingerprint(ids(5000, 5800)); // someone else's server
    CHECK(home.size() >= 10 && home.size() <= 128);
    CHECK(fingerprint_match(home, away));
    CHECK(fingerprint_match(home, grown));
    CHECK(!fingerprint_match(home, other));
    CHECK(!fingerprint_match(home, library_fingerprint({})));

    Config cfg;
    const std::string legacy = cfg.account_id("http://home:4533", "anna");
    CHECK(legacy.size() == 8);
    cfg.accounts.push_back({legacy, "anna", {"http://home:4533"}, home});
    CHECK(cfg.account_id("http://home:4533", "anna") == legacy);
    CHECK(cfg.find_account("https://vpn.example", "anna") == nullptr);
    CHECK(cfg.match_account("anna", away) != nullptr);
    CHECK(cfg.match_account("bob", away) == nullptr);        // another user on the same server is its own account
    CHECK(cfg.match_account("anna", other) == nullptr);
    cfg.match_account("anna", away)->urls.push_back("https://vpn.example");
    CHECK(cfg.account_id("https://vpn.example", "anna") == legacy);

    const std::string path = (std::filesystem::temp_directory_path() / "xf_account_test.json").string();
    CHECK(cfg.save(path));
    Config back;
    CHECK(back.load(path));
    CHECK(back.accounts.size() == 1 && back.accounts[0].urls.size() == 2 && back.accounts[0].fp == home);
    CHECK(back.account_id("https://vpn.example", "anna") == legacy);
    std::filesystem::remove(path);
    std::printf(fails ? "FAILED\n" : "OK\n");
    return fails ? 1 : 0;
}
