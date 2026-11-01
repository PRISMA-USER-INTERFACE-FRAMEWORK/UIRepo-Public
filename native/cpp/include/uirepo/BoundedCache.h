#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <unordered_map>
#include <utility>

namespace uirepo::render {

struct CacheStats {
    std::size_t entries{};
    std::size_t bytes{};
    std::uint64_t hits{};
    std::uint64_t misses{};
    std::uint64_t inserts{};
    std::uint64_t evictions{};
    std::uint64_t rejections{};
};

template <class Key, class Value, class Hash = std::hash<Key>, class Equal = std::equal_to<Key>>
class BoundedLruCache final {
public:
    explicit BoundedLruCache(std::size_t maxBytes, std::size_t maxEntries)
        : maxBytes_(maxBytes),
          maxEntries_(maxEntries) {}

    BoundedLruCache(const BoundedLruCache&) = delete;
    BoundedLruCache& operator=(const BoundedLruCache&) = delete;

    [[nodiscard]] Value* find(const Key& key) noexcept {
        const auto found = entries_.find(key);
        if (found == entries_.end()) {
            ++stats_.misses;
            return nullptr;
        }

        touch(found->second);
        ++stats_.hits;
        return std::addressof(found->second.value);
    }

    [[nodiscard]] const Value* find(const Key& key) const noexcept {
        const auto found = entries_.find(key);
        if (found == entries_.end()) {
            return nullptr;
        }
        return std::addressof(found->second.value);
    }

    template <class V>
    [[nodiscard]] bool insert(Key key, V&& value, std::size_t bytes) {
        if (maxBytes_ == 0 || maxEntries_ == 0 || bytes > maxBytes_) {
            ++stats_.rejections;
            return false;
        }

        if (auto found = entries_.find(key); found != entries_.end()) {
            remove(found);
        }

        evictFor(bytes, 1);
        if (bytes > maxBytes_ - stats_.bytes || entries_.size() >= maxEntries_) {
            ++stats_.rejections;
            return false;
        }

        lru_.push_front(key);
        Entry entry{
            .value = std::forward<V>(value),
            .bytes = bytes,
            .lru = lru_.begin(),
        };
        try {
            entries_.emplace(std::move(key), std::move(entry));
        } catch (...) {
            lru_.pop_front();
            throw;
        }

        stats_.bytes += bytes;
        stats_.entries = entries_.size();
        ++stats_.inserts;
        return true;
    }

    [[nodiscard]] bool erase(const Key& key) noexcept {
        const auto found = entries_.find(key);
        if (found == entries_.end()) {
            return false;
        }
        remove(found);
        return true;
    }

    void clear() noexcept {
        entries_.clear();
        lru_.clear();
        stats_.entries = 0;
        stats_.bytes = 0;
    }

    [[nodiscard]] const CacheStats& stats() const noexcept { return stats_; }
    [[nodiscard]] std::size_t maxBytes() const noexcept { return maxBytes_; }
    [[nodiscard]] std::size_t maxEntries() const noexcept { return maxEntries_; }

private:
    struct Entry {
        Value value;
        std::size_t bytes{};
        typename std::list<Key>::iterator lru;
    };

    using Map = std::unordered_map<Key, Entry, Hash, Equal>;
    using Iterator = typename Map::iterator;

    void touch(Entry& entry) noexcept {
        if (entry.lru == lru_.begin()) {
            return;
        }
        lru_.splice(lru_.begin(), lru_, entry.lru);
        entry.lru = lru_.begin();
    }

    void remove(Iterator found) noexcept {
        stats_.bytes -= found->second.bytes;
        lru_.erase(found->second.lru);
        entries_.erase(found);
        stats_.entries = entries_.size();
    }

    void evictFor(std::size_t additionalBytes, std::size_t additionalEntries) noexcept {
        while (!entries_.empty() &&
               (additionalBytes > maxBytes_ - stats_.bytes ||
                additionalEntries > maxEntries_ - entries_.size())) {
            const Key& victimKey = lru_.back();
            const auto victim = entries_.find(victimKey);
            if (victim == entries_.end()) {
                lru_.pop_back();
                continue;
            }
            remove(victim);
            ++stats_.evictions;
        }
    }

    std::size_t maxBytes_{};
    std::size_t maxEntries_{};
    std::list<Key> lru_;
    Map entries_;
    CacheStats stats_{};
};

} // namespace uirepo::render
