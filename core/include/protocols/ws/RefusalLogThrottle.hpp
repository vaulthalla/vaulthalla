#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace vh::protocols::ws {

// Bounds the log volume of refused ws requests (#135). Any client that reaches /ws can send refused requests
// (rate-limited, unauthorized) as fast as it likes; logging each one at warning let it write to the daemon log
// at that rate. The first refusal per label (client + command + reason) per window is reported for logging at
// warning; the rest are only counted, and the count comes back once as a summary when the window has closed (the
// next time the label is seen, or when a later sweep retires it).
//
// Memory is bounded: at most `maxEntries` labels are tracked. When every slot holds a live window, refusals for
// new labels are counted in one overflow bucket (reported as a summary too) instead of growing the map or
// producing more warnings, so neither memory nor warnings per window grow with the number of distinct labels.
class RefusalLogThrottle {
public:
    using Clock = std::chrono::steady_clock;

    static constexpr auto kDefaultWindow = std::chrono::seconds{60};
    static constexpr std::size_t kDefaultMaxEntries = 512;
    static constexpr const char* kOverflowLabel = "Refusals for further clients/commands (log throttle full)";

    struct Summary {
        std::string label;
        uint64_t suppressed{};
        std::chrono::seconds span{0};  // first to last refusal of the closed window, rounded up
    };

    struct Decision {
        bool warn{};                     // first refusal for this label in its window: log it at warning
        std::vector<Summary> summaries;  // closed windows to report now
    };

    explicit RefusalLogThrottle(std::chrono::seconds window = kDefaultWindow,
                                std::size_t maxEntries = kDefaultMaxEntries);

    [[nodiscard]] Decision record(const std::string& label, Clock::time_point now = Clock::now());

    [[nodiscard]] std::size_t size() const;
    void reset();

private:
    struct Entry {
        Clock::time_point window_start{};
        Clock::time_point last_seen{};
        uint64_t suppressed{};
    };

    [[nodiscard]] Summary summaryOf(const std::string& label, const Entry& entry) const;
    void sweepClosed(Clock::time_point now, Decision& decision);

    std::chrono::seconds window_;
    std::size_t max_entries_;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, Entry> entries_;
    Clock::time_point last_sweep_{};
    bool overflow_active_{false};
    Entry overflow_{};
};

}
