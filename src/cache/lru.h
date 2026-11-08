// Bounded cold-page cache: byte-budgeted LRU over loadable pages. Loading happens outside the lock
// so a slow disk read never blocks other readers; two racing loads of one key keep the first result.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace xf::cache {
class PageCache {
public:
    using Page = std::shared_ptr<const std::string>;
    using Loader = std::function<bool(const std::string& key, std::string& bytes)>;
    PageCache(size_t max_pages, size_t max_bytes, Loader loader) : max_pages_(max_pages ? max_pages : 1), max_bytes_(max_bytes), loader_(std::move(loader)) {}
    Page get(const std::string& key) {
        { std::lock_guard<std::mutex> lk(m_); auto it = map_.find(key); if (it != map_.end()) { order_.splice(order_.begin(), order_, it->second); ++hits_; return it->second->page; } ++misses_; }
        std::string bytes; if (!loader_ || !loader_(key, bytes)) return nullptr;
        Page page = std::make_shared<const std::string>(std::move(bytes));
        std::lock_guard<std::mutex> lk(m_);
        auto it = map_.find(key); if (it != map_.end()) { order_.splice(order_.begin(), order_, it->second); return it->second->page; }
        order_.push_front({key, page}); map_[key] = order_.begin(); bytes_ += page->size(); evict_locked();
        return page;
    }
    void drop(const std::string& key) { std::lock_guard<std::mutex> lk(m_); auto it = map_.find(key); if (it == map_.end()) return; bytes_ -= it->second->page->size(); order_.erase(it->second); map_.erase(it); }
    size_t pages() const { std::lock_guard<std::mutex> lk(m_); return map_.size(); }
    size_t bytes() const { std::lock_guard<std::mutex> lk(m_); return bytes_; }
    uint64_t hits() const { std::lock_guard<std::mutex> lk(m_); return hits_; }
    uint64_t misses() const { std::lock_guard<std::mutex> lk(m_); return misses_; }
private:
    struct Entry { std::string key; Page page; };
    void evict_locked() {
        while (order_.size() > 1 && (map_.size() > max_pages_ || (max_bytes_ && bytes_ > max_bytes_))) {
            Entry& last = order_.back(); bytes_ -= last.page->size(); map_.erase(last.key); order_.pop_back();
        }
    }
    size_t max_pages_, max_bytes_, bytes_ = 0; uint64_t hits_ = 0, misses_ = 0;
    Loader loader_; mutable std::mutex m_;
    std::list<Entry> order_; std::unordered_map<std::string, std::list<Entry>::iterator> map_;
};
} // namespace xf::cache
