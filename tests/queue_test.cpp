#include <cstdio>
#include <set>
#include "../src/app/queue.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

static std::vector<Song> mk(int n) { std::vector<Song> v; for (int i = 0; i < n; ++i) { Song s; s.id = "s" + std::to_string(i); s.title = "T" + std::to_string(i); v.push_back(s); } return v; }

int main() {
    Queue q;
    CHECK(q.empty() && q.current() == nullptr && q.next() == nullptr && q.on_track_end() == nullptr);
    q.set(mk(4), 1);
    CHECK(q.current()->id == "s1");
    CHECK(q.next()->id == "s2" && q.next()->id == "s3" && q.next()->id == "s0");  // wraps regardless of repeat
    CHECK(q.prev()->id == "s3");
    q.set_repeat(Repeat::One); CHECK(q.on_track_end()->id == "s3");
    q.set_repeat(Repeat::Off); CHECK(q.on_track_end()->id == "s0");

    q.set(mk(5), 2);
    q.play_next(mk(1)[0]);  // becomes s0-like id "s0" duplicate; check position only
    CHECK(q.size() == 6 && q.songs()[3].id == "s0" && q.index() == 2);
    CHECK(q.next()->id == "s0");
    q.remove(3);  // remove the inserted one while it is current
    CHECK(q.size() == 5 && q.current()->id == "s3");
    q.remove(0); CHECK(q.current()->id == "s3" && q.index() == 2);
    q.move(0, 3); CHECK(q.current()->id == "s3" && q.songs()[3].id == "s1");
    q.clear(); CHECK(q.empty() && q.next() == nullptr);

    // shuffle: every song exactly once per cycle, current first, order stable on repeated cycles
    q.seed(42); q.set(mk(50), 7); q.set_shuffle(true);
    CHECK(q.current()->id == "s7");
    std::set<std::string> seen{q.current()->id};
    for (int i = 0; i < 49; ++i) CHECK(seen.insert(q.next()->id).second);
    CHECK(seen.size() == 50);
    CHECK(q.next()->id == "s7");                      // second cycle starts again at the same pre-shuffled order
    q.set_shuffle(false); CHECK(q.current()->id == "s7" && q.next()->id == "s8");
    // add while shuffled is included in the cycle
    q.set_shuffle(true); q.add(mk(1)[0]); CHECK(q.size() == 51);
    std::set<std::string> s2; for (int i = 0; i < 51; ++i) s2.insert(q.songs()[0].id), q.next();
    uint32_t r = q.revision(); q.add(mk(1)[0]); CHECK(q.revision() == r + 1);

    // persistence round trip
    Queue a; a.set(mk(3), 0);
    a.add([] { Song s; s.id = "x"; s.title = "Ünï \"q\""; s.size = 5000000000ULL; s.starred = true; return s; }());
    CHECK(a.save_songs("queue_test_tmp.json"));
    Queue b; CHECK(b.load_songs("queue_test_tmp.json"));
    CHECK(b.size() == 4 && b.songs()[3].title == "Ünï \"q\"" && b.songs()[3].size == 5000000000ULL && b.songs()[3].starred);
    CHECK(!b.load_songs("/nonexistent/q.json"));
    std::remove("queue_test_tmp.json");
    std::printf(fails ? "FAILED\n" : "OK\n");
    return fails ? 1 : 0;
}
