#include "protocols/ws/RefusalLogThrottle.hpp"

#include <algorithm>
#include <stdexcept>

namespace vh::protocols::ws {

RefusalLogThrottle::RefusalLogThrottle(const std::chrono::seconds window, const std::size_t maxEntries)
    : window_(window), max_entries_(maxEntries) {
    if (window_ <= std::chrono::seconds{0}) throw std::invalid_argument("Refusal log window is required");
    if (max_entries_ == 0) throw std::invalid_argument("Refusal log throttle needs at least one entry");
}

RefusalLogThrottle::Decision RefusalLogThrottle::record(const std::string& label, const Clock::time_point now) {
    Decision decision;
    std::scoped_lock lock(mutex_);

    // Retire closed windows at most once per window, so labels that stop arriving still report their count and
    // free their slot without making every refusal walk the map.
    if (now - last_sweep_ >= window_) sweepClosed(now, decision);

    if (const auto it = entries_.find(label); it != entries_.end()) {
        auto& entry = it->second;
        if (now - entry.window_start < window_) {
            ++entry.suppressed;
            entry.last_seen = now;
            return decision;
        }
        if (entry.suppressed > 0) decision.summaries.push_back(summaryOf(label, entry));
        entry = Entry{.window_start = now, .last_seen = now, .suppressed = 0};
        decision.warn = true;
        return decision;
    }

    // Full: one extra sweep the first time this window, then count new labels in the overflow bucket.
    if (entries_.size() >= max_entries_ && !overflow_active_) sweepClosed(now, decision);

    if (entries_.size() < max_entries_) {
        entries_.emplace(label, Entry{.window_start = now, .last_seen = now, .suppressed = 0});
        decision.warn = true;
        return decision;
    }

    if (!overflow_active_) {
        overflow_active_ = true;
        overflow_ = Entry{.window_start = now, .last_seen = now, .suppressed = 0};
        decision.warn = true;
        return decision;
    }

    ++overflow_.suppressed;
    overflow_.last_seen = now;
    return decision;
}

RefusalLogThrottle::Summary RefusalLogThrottle::summaryOf(const std::string& label, const Entry& entry) const {
    const auto elapsed = entry.last_seen - entry.window_start;
    auto span = std::chrono::ceil<std::chrono::seconds>(elapsed);
    span = std::clamp(span, std::chrono::seconds{1}, window_);
    return {.label = label, .suppressed = entry.suppressed, .span = span};
}

void RefusalLogThrottle::sweepClosed(const Clock::time_point now, Decision& decision) {
    last_sweep_ = now;
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (now - it->second.window_start < window_) {
            ++it;
            continue;
        }
        if (it->second.suppressed > 0) decision.summaries.push_back(summaryOf(it->first, it->second));
        it = entries_.erase(it);
    }

    if (overflow_active_ && now - overflow_.window_start >= window_) {
        if (overflow_.suppressed > 0) decision.summaries.push_back(summaryOf(kOverflowLabel, overflow_));
        overflow_active_ = false;
        overflow_ = Entry{};
    }
}

std::size_t RefusalLogThrottle::size() const {
    std::scoped_lock lock(mutex_);
    return entries_.size();
}

void RefusalLogThrottle::reset() {
    std::scoped_lock lock(mutex_);
    entries_.clear();
    last_sweep_ = Clock::time_point{};
    overflow_active_ = false;
    overflow_ = Entry{};
}

}
