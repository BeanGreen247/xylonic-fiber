// Play queue: ordering, shuffle (pre-shuffled order, each track once per cycle), repeat, wrap-around.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "../subsonic/model.h"

enum class Repeat : uint8_t { Off, All, One };

class Queue {
public:
    // Replaces the queue. start is an index into songs.
    void set(std::vector<Song> songs, size_t start);
    void add(Song s);
    void append(std::vector<Song> songs);  // bulk add; in shuffle mode the new tracks are mixed into the unplayed rest
    void play_next(Song s);  // insert right after the current track
    void remove(size_t i);
    void move(size_t from, size_t to);
    void clear();

    size_t size() const { return songs_.size(); }
    bool empty() const { return songs_.empty(); }
    const std::vector<Song>& songs() const { return songs_; }
    const Song* current() const { return songs_.empty() ? nullptr : &songs_[cur_]; }
    size_t index() const { return cur_; }
    bool jump(size_t i);  // make i current; false if out of range

    // Manual next/prev. Both wrap around at the ends regardless of repeat mode.
    const Song* next();
    const Song* prev();
    // Up to each_side tracks before and after the current one in play order (what skip back/forward will reach).
    std::vector<Song> neighbours(size_t each_side) const;
    // Natural end of the current track: repeat-one stays, otherwise next().
    const Song* on_track_end();

    bool shuffle() const { return shuffle_; }
    void set_shuffle(bool on);
    Repeat repeat() const { return repeat_; }
    void set_repeat(Repeat r) { repeat_ = r; }

    // Persistence: songs are written only when the content changes; position state is tiny.
    bool save_songs(const std::string& path) const;
    bool load_songs(const std::string& path);
    uint32_t revision() const { return rev_; }  // bumps on every content change

    void seed(uint64_t s) { rng_ = s ? s : 1; }

private:
    void reshuffle_keep_current();
    uint32_t rand32();
    std::vector<Song> songs_;
    std::vector<size_t> order_;  // play order (indices into songs_); identity when not shuffled
    size_t pos_ = 0;             // position in order_
    size_t cur_ = 0;             // == order_[pos_]
    bool shuffle_ = false;
    Repeat repeat_ = Repeat::Off;
    uint32_t rev_ = 0;
    uint64_t rng_ = 88172645463325252ULL;
};
