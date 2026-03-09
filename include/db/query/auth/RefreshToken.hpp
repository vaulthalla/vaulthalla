#pragma once

#include <memory>
#include <string>
#include <vector>

namespace vh::auth::model { struct RefreshToken; }
namespace vh::identities::model { struct User; }

namespace vh::db::query::auth {

struct RefreshToken {
    static void set(const std::shared_ptr<vh::auth::model::RefreshToken>& token);
    static void remove(const std::string& jti);

    static std::shared_ptr<vh::auth::model::RefreshToken> get(const std::string& jti);
    static std::vector<std::shared_ptr<vh::auth::model::RefreshToken>> list(unsigned int userId);

    static void touch(const std::string& jti);
    static void refresh(const std::string& jti);
    static void revokeAll(unsigned int userId);

    static void purgeExpired(unsigned int userId);
    static void purgeOldRevoked();
    static void revokeAndPurge(unsigned int userId);

    static std::shared_ptr<vh::identities::model::User> getUserByJti(const std::string& jti);
};

}
