#include "queue.h"
#include <algorithm>
#include <cstdio>
#include "../../third_party/yyjson.h"

uint32_t Queue::rand32() {  // xorshift64*
    rng_ ^= rng_ >> 12; rng_ ^= rng_ << 25; rng_ ^= rng_ >> 27;
    return static_cast<uint32_t>((rng_ * 2685821657736338717ULL) >> 32);
}

void Queue::reshuffle_keep_current() {
    order_.resize(songs_.size());
    for (size_t i = 0; i < order_.size(); ++i) order_[i] = i;
    if (shuffle_ && order_.size() > 1) {
        for (size_t i = order_.size() - 1; i > 0; --i) std::swap(order_[i], order_[rand32() % (i + 1)]);
        auto it = std::find(order_.begin(), order_.end(), cur_);
        std::iter_swap(order_.begin(), it);  // current track first, rest random
        pos_ = 0;
    } else {
        pos_ = cur_;
    }
}

void Queue::set(std::vector<Song> songs, size_t start) {
    songs_ = std::move(songs);
    cur_ = start < songs_.size() ? start : 0;
    reshuffle_keep_current();
    ++rev_;
}

void Queue::add(Song s) {
    songs_.push_back(std::move(s));
    size_t i = songs_.size() - 1;
    if (songs_.size() == 1) { cur_ = 0; order_ = {0}; pos_ = 0; }
    else order_.push_back(i);
    ++rev_;
}

void Queue::play_next(Song s) {
    if (songs_.empty()) { add(std::move(s)); return; }
    size_t at = cur_ + 1;
    songs_.insert(songs_.begin() + at, std::move(s));
    for (auto& o : order_) if (o >= at) ++o;
    order_.insert(order_.begin() + pos_ + 1, at);
    ++rev_;
}

void Queue::remove(size_t i) {
    if (i >= songs_.size()) return;
    songs_.erase(songs_.begin() + i);
    order_.erase(std::find(order_.begin(), order_.end(), i));
    for (auto& o : order_) if (o > i) --o;
    if (songs_.empty()) { cur_ = pos_ = 0; }
    else {
        if (i < cur_) --cur_;
        else if (i == cur_ && cur_ >= songs_.size()) cur_ = 0;
        pos_ = static_cast<size_t>(std::find(order_.begin(), order_.end(), cur_) - order_.begin());
    }
    ++rev_;
}

void Queue::move(size_t from, size_t to) {
    if (from >= songs_.size() || to >= songs_.size() || from == to) return;
    Song s = std::move(songs_[from]);
    songs_.erase(songs_.begin() + from);
    songs_.insert(songs_.begin() + to, std::move(s));
    auto remap = [&](size_t v) {
        if (v == from) return to;
        if (from < to && v > from && v <= to) return v - 1;
        if (from > to && v >= to && v < from) return v + 1;
        return v;
    };
    for (auto& o : order_) o = remap(o);
    cur_ = remap(cur_);
    pos_ = static_cast<size_t>(std::find(order_.begin(), order_.end(), cur_) - order_.begin());
    ++rev_;
}

void Queue::clear() {
    songs_.clear(); order_.clear(); cur_ = pos_ = 0;
    ++rev_;
}

bool Queue::jump(size_t i) {
    if (i >= songs_.size()) return false;
    cur_ = i;
    pos_ = static_cast<size_t>(std::find(order_.begin(), order_.end(), i) - order_.begin());
    return true;
}

const Song* Queue::next() {
    if (songs_.empty()) return nullptr;
    pos_ = (pos_ + 1) % order_.size();
    cur_ = order_[pos_];
    return &songs_[cur_];
}

const Song* Queue::prev() {
    if (songs_.empty()) return nullptr;
    pos_ = (pos_ + order_.size() - 1) % order_.size();
    cur_ = order_[pos_];
    return &songs_[cur_];
}

const Song* Queue::on_track_end() { return repeat_ == Repeat::One ? current() : next(); }

void Queue::set_shuffle(bool on) {
    if (on == shuffle_) return;
    shuffle_ = on;
    if (!songs_.empty()) reshuffle_keep_current();
}

bool Queue::save_songs(const std::string& path) const {
    yyjson_mut_doc* d = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* arr = yyjson_mut_arr(d);
    yyjson_mut_doc_set_root(d, arr);
    for (const Song& s : songs_) {
        yyjson_mut_val* o = yyjson_mut_arr_add_obj(d, arr);
        yyjson_mut_obj_add_strcpy(d, o, "id", s.id.c_str());
        yyjson_mut_obj_add_strcpy(d, o, "title", s.title.c_str());
        yyjson_mut_obj_add_strcpy(d, o, "album", s.album.c_str());
        yyjson_mut_obj_add_strcpy(d, o, "albumId", s.album_id.c_str());
        yyjson_mut_obj_add_strcpy(d, o, "artist", s.artist.c_str());
        yyjson_mut_obj_add_strcpy(d, o, "artistId", s.artist_id.c_str());
        yyjson_mut_obj_add_strcpy(d, o, "coverArt", s.cover_art.c_str());
        yyjson_mut_obj_add_strcpy(d, o, "suffix", s.suffix.c_str());
        yyjson_mut_obj_add_int(d, o, "track", s.track);
        yyjson_mut_obj_add_int(d, o, "year", s.year);
        yyjson_mut_obj_add_int(d, o, "duration", s.duration);
        yyjson_mut_obj_add_int(d, o, "bitRate", s.bit_rate);
        yyjson_mut_obj_add_uint(d, o, "size", s.size);
        yyjson_mut_obj_add_bool(d, o, "starred", s.starred);
    }
    std::string tmp = path + ".tmp";
    bool ok = yyjson_mut_write_file(tmp.c_str(), d, 0, nullptr, nullptr);
    yyjson_mut_doc_free(d);
    return ok && std::rename(tmp.c_str(), path.c_str()) == 0;
}

bool Queue::load_songs(const std::string& path) {
    yyjson_doc* d = yyjson_read_file(path.c_str(), 0, nullptr, nullptr);
    if (!d) return false;
    yyjson_val* root = yyjson_doc_get_root(d);
    if (!yyjson_is_arr(root)) { yyjson_doc_free(d); return false; }
    auto str = [](yyjson_val* o, const char* k) { yyjson_val* v = yyjson_obj_get(o, k); return v && yyjson_is_str(v) ? std::string(yyjson_get_str(v)) : std::string(); };
    auto num = [](yyjson_val* o, const char* k) { yyjson_val* v = yyjson_obj_get(o, k); return v && yyjson_is_num(v) ? static_cast<int>(yyjson_get_sint(v)) : 0; };
    std::vector<Song> v;
    size_t i, n; yyjson_val* o;
    yyjson_arr_foreach(root, i, n, o) {
        Song s;
        s.id = str(o, "id"); s.title = str(o, "title"); s.album = str(o, "album"); s.album_id = str(o, "albumId");
        s.artist = str(o, "artist"); s.artist_id = str(o, "artistId"); s.cover_art = str(o, "coverArt"); s.suffix = str(o, "suffix");
        s.track = num(o, "track"); s.year = num(o, "year"); s.duration = num(o, "duration"); s.bit_rate = num(o, "bitRate");
        yyjson_val* sz = yyjson_obj_get(o, "size"); s.size = sz && yyjson_is_num(sz) ? yyjson_get_uint(sz) : 0;
        yyjson_val* st = yyjson_obj_get(o, "starred"); s.starred = st && yyjson_get_bool(st);
        if (!s.id.empty()) v.push_back(std::move(s));
    }
    yyjson_doc_free(d);
    set(std::move(v), 0);
    return true;
}
