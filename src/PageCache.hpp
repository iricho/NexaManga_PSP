#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <unordered_map>
#include <vector>

struct PageCacheStats {
    std::size_t hits = 0;
    std::size_t misses = 0;
    std::size_t evictions = 0;
    std::size_t requiredLoads = 0;
    std::size_t preloadLoads = 0;
    std::size_t preloadFailures = 0;
    std::size_t budgetRejections = 0;
    std::size_t preloadBudgetRejections = 0;
};

template <typename T>
class PageCache {
public:
    explicit PageCache(std::size_t budgetBytes = 0, std::size_t fixedBytes = 0)
        : budgetBytes_(budgetBytes), fixedBytes_(fixedBytes) {
    }

    void configure(std::size_t budgetBytes, std::size_t fixedBytes) {
        budgetBytes_ = budgetBytes;
        fixedBytes_ = fixedBytes;
        enforceBudget(noIndex(), false, 0, noIndex());
    }

    void clear() {
        entries_.clear();
        residentBytes_ = 0;
        clock_ = 0;
    }

    std::shared_ptr<T> get(std::size_t index) {
        auto found = entries_.find(index);
        if (found == entries_.end()) {
            ++stats_.misses;
            return {};
        }
        ++stats_.hits;
        found->second.lastUse = ++clock_;
        return found->second.value;
    }

    std::shared_ptr<T> peek(std::size_t index) const {
        const auto found = entries_.find(index);
        return found == entries_.end() ? std::shared_ptr<T>() : found->second.value;
    }

    bool insert(std::size_t index, std::shared_ptr<T> value, std::size_t bytes,
                std::size_t protectedIndex, std::size_t pageCount, bool preload) {
        if (!value) return false;

        const auto existing = entries_.find(index);
        if (existing != entries_.end()) {
            residentBytes_ -= existing->second.bytes;
            entries_.erase(existing);
        }

        if (budgetBytes_ > 0 &&
            (fixedBytes_ > budgetBytes_ || bytes > budgetBytes_ - fixedBytes_)) {
            ++stats_.budgetRejections;
            if (preload) ++stats_.preloadBudgetRejections;
            return false;
        }

        Entry entry;
        entry.value = std::move(value);
        entry.bytes = bytes;
        entry.lastUse = ++clock_;
        entries_.emplace(index, std::move(entry));
        residentBytes_ += bytes;

        if (!enforceBudget(protectedIndex, preload, pageCount, index)) {
            const auto inserted = entries_.find(index);
            if (inserted != entries_.end()) {
                residentBytes_ -= inserted->second.bytes;
                entries_.erase(inserted);
            }
            ++stats_.budgetRejections;
            if (preload) ++stats_.preloadBudgetRejections;
            return false;
        }

        if (preload) ++stats_.preloadLoads;
        else ++stats_.requiredLoads;
        return true;
    }

    void rebalance(std::size_t currentIndex, std::size_t pageCount) {
        std::vector<std::size_t> remove;
        for (const auto& item : entries_) {
            if (!inWindow(item.first, currentIndex, pageCount)) remove.push_back(item.first);
        }
        std::sort(remove.begin(), remove.end());
        for (std::size_t index : remove) erase(index, true);
        enforceBudget(currentIndex, false, pageCount, noIndex());
    }

    void trimToCurrent(std::size_t currentIndex) {
        std::vector<std::size_t> remove;
        for (const auto& item : entries_) {
            if (item.first != currentIndex) remove.push_back(item.first);
        }
        std::sort(remove.begin(), remove.end());
        for (std::size_t index : remove) erase(index, true);
    }

    void recordPreloadFailure(bool budgetRejected = false) {
        ++stats_.preloadFailures;
        if (budgetRejected) {
            ++stats_.budgetRejections;
            ++stats_.preloadBudgetRejections;
        }
    }

    bool contains(std::size_t index) const { return entries_.find(index) != entries_.end(); }
    std::size_t entryCount() const { return entries_.size(); }
    std::size_t residentBytes() const { return residentBytes_; }
    std::size_t fixedBytes() const { return fixedBytes_; }
    std::size_t totalTrackedBytes() const { return fixedBytes_ + residentBytes_; }
    std::size_t budgetBytes() const { return budgetBytes_; }
    const PageCacheStats& stats() const { return stats_; }
    std::size_t bytesForIndex(std::size_t index) const {
        const auto found = entries_.find(index);
        return found == entries_.end() ? 0 : found->second.bytes;
    }

    std::vector<std::size_t> indices() const {
        std::vector<std::size_t> result;
        result.reserve(entries_.size());
        for (const auto& item : entries_) result.push_back(item.first);
        std::sort(result.begin(), result.end());
        return result;
    }

private:
    struct Entry {
        std::shared_ptr<T> value;
        std::size_t bytes = 0;
        std::uint64_t lastUse = 0;
    };

    static std::size_t noIndex() { return std::numeric_limits<std::size_t>::max(); }

    static bool inWindow(std::size_t index, std::size_t current, std::size_t pageCount) {
        if (current == noIndex() || current >= pageCount) return false;
        if (index == current) return true;
        if (current > 0 && index == current - 1) return true;
        return current + 1 < pageCount && index == current + 1;
    }

    bool overBudget() const {
        return budgetBytes_ > 0 &&
               (fixedBytes_ > budgetBytes_ || residentBytes_ > budgetBytes_ - fixedBytes_);
    }

    bool enforceBudget(std::size_t protectedIndex, bool preload, std::size_t pageCount,
                       std::size_t secondaryProtectedIndex) {
        while (overBudget()) {
            std::size_t victim = noIndex();
            std::uint64_t oldest = std::numeric_limits<std::uint64_t>::max();
            for (const auto& item : entries_) {
                if (item.first == protectedIndex || item.first == secondaryProtectedIndex) continue;
                if (preload && inWindow(item.first, protectedIndex, pageCount)) continue;
                if (item.second.lastUse < oldest ||
                    (item.second.lastUse == oldest && item.first < victim)) {
                    victim = item.first;
                    oldest = item.second.lastUse;
                }
            }
            if (victim == noIndex()) return false;
            erase(victim, true);
        }
        return true;
    }

    void erase(std::size_t index, bool eviction) {
        const auto found = entries_.find(index);
        if (found == entries_.end()) return;
        residentBytes_ -= found->second.bytes;
        entries_.erase(found);
        if (eviction) ++stats_.evictions;
    }

    std::unordered_map<std::size_t, Entry> entries_;
    std::size_t budgetBytes_ = 0;
    std::size_t fixedBytes_ = 0;
    std::size_t residentBytes_ = 0;
    std::uint64_t clock_ = 0;
    PageCacheStats stats_;
};
