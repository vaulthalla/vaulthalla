#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace vh::share { struct EmailChallenge; }

namespace vh::db::query::share {

struct EmailChallenge {
    static std::shared_ptr<vh::share::EmailChallenge> create(const std::shared_ptr<vh::share::EmailChallenge>& challenge);
    static std::shared_ptr<vh::share::EmailChallenge> get(const std::string& id);
    static std::shared_ptr<vh::share::EmailChallenge> getActive(const std::string& share_id, const std::vector<uint8_t>& email_hash);
    static void recordAttempt(const std::string& challenge_id);
    static void consume(const std::string& challenge_id);
    static uint64_t purgeExpired();
};

}
