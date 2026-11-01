#include <cstdio>
#include <fstream>
#include "../src/cache/journal.h"
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)
using namespace xf::cache;
int main() {
    const char* journal_path = "journal_test.bin";
    const char* snapshot_path = "snapshot_test.bin";
    std::remove(journal_path); std::remove(snapshot_path); std::remove("snapshot_test.bin.tmp");
    Journal journal(journal_path);
    CHECK(journal.append_transaction(1, 4, {"one", "two"}));
    CHECK(journal.append_transaction(2, 5, {"three"}));
    std::vector<JournalRecord> records; CHECK(journal.recover(records)); CHECK(records.size() == 3 && records[0].payload == "one" && records[2].generation == 5);
    { std::ofstream out(journal_path, std::ios::binary | std::ios::app); out << "partial"; }
    records.clear(); CHECK(journal.recover(records) && records.size() == 3);
    CHECK(write_snapshot(snapshot_path, "snapshot payload")); std::string payload; CHECK(read_snapshot(snapshot_path, payload) && payload == "snapshot payload");
    { std::fstream file(snapshot_path, std::ios::in | std::ios::out | std::ios::binary); file.seekp(40); char bad = 'X'; file.write(&bad, 1); }
    CHECK(!read_snapshot(snapshot_path, payload));
    std::remove(journal_path); std::remove(snapshot_path); std::remove("snapshot_test.bin.tmp");
    std::printf(fails ? "FAILED\n" : "OK\n"); return fails ? 1 : 0;
}
