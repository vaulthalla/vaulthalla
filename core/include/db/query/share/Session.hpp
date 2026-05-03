#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace vh::share { struct Session; }

namespace vh::db::query::share {

struct Session {
    static std::shared_ptr<vh::share::Session> create(const std::shared_ptr<vh::share::Session>& session);
    static std::shared_ptr<vh::share::Session> get(const std::string& id);
    static std::shared_ptr<vh::share::Session> getByLookupId(const std::string& lookup_id);
    static void verify(const std::string& session_id, const std::vector<uint8_t>& email_hash);
    static void touch(const std::string& session_id);
    static void revoke(const std::string& session_id);
    static void revokeForShare(const std::string& share_id);
    static uint64_t purgeExpired();
};

}
