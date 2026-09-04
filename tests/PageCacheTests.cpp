#include "PageCache.hpp"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

std::shared_ptr<int> page(int value) { return std::make_shared<int>(value); }

void testHitsMissesWindowAndEviction() {
    PageCache<int> cache(100, 10);
    expect(!cache.get(1), "cache miss returns empty");
    expect(cache.insert(1, page(1), 30, static_cast<std::size_t>(-1), 5, false),
           "required current page inserts");
    expect(cache.get(1) && *cache.get(1) == 1, "cache hit returns the decoded page");
    expect(cache.insert(2, page(2), 30, 1, 5, false), "required next page inserts");
    expect(cache.insert(0, page(0), 30, 1, 5, true), "previous page preloads within budget");
    expect(cache.indices() == std::vector<std::size_t>({0, 1, 2}),
           "cache is bounded to N-1/N/N+1");

    cache.rebalance(2, 5);
    expect(cache.indices() == std::vector<std::size_t>({1, 2}),
           "rebalance deterministically evicts pages outside the new window");
    expect(cache.insert(3, page(3), 30, 2, 5, true), "new forward neighbor preloads");
    expect(cache.totalTrackedBytes() == 100, "fixed and decoded bytes share one budget");
    expect(cache.stats().misses == 1 && cache.stats().hits == 2,
           "hit and miss instrumentation is exposed");
    expect(cache.stats().evictions == 1, "window eviction is instrumented");
}

void testBudgetAndPreloadRecovery() {
    PageCache<int> cache(70, 10);
    expect(cache.insert(1, page(1), 30, static_cast<std::size_t>(-1), 4, false),
           "current page fits constrained budget");
    expect(cache.insert(2, page(2), 30, 1, 4, true), "one neighbor fits constrained budget");
    expect(!cache.insert(0, page(0), 30, 1, 4, true),
           "preload does not evict another useful window page");
    expect(cache.indices() == std::vector<std::size_t>({1, 2}),
           "rejected preload leaves resident pages intact");
    expect(!cache.insert(3, page(3), 70, 1, 4, false),
           "single page larger than the available budget is rejected");

    cache.recordPreloadFailure();
    cache.rebalance(0, 4);
    expect(cache.insert(0, page(0), 30, 1, 4, false),
           "a failed preload does not poison a later required load");
    expect(cache.stats().preloadFailures == 1 && cache.stats().budgetRejections >= 2,
           "preload failures and budget rejections are instrumented");
}

} // namespace

int main() {
    testHitsMissesWindowAndEviction();
    testBudgetAndPreloadRecovery();
    if (failures) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All page cache tests passed\n";
    return 0;
}
