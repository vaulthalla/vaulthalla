#pragma once

#include "db/model/ListQueryParams.hpp"

#include <memory>
#include <vector>

namespace vh::vault::model { struct APIKey; }
namespace vh::identities { struct User; }

namespace vh::db::query::vault {

class APIKey {
    using AK = vh::vault::model::APIKey;
    using APIKeyPtr = std::shared_ptr<AK>;

public:
    APIKey() = default;

    static unsigned int upsertAPIKey(const APIKeyPtr& key);

    static void removeAPIKey(unsigned int keyId);

    static std::vector<APIKeyPtr> listAPIKeys(unsigned int userId, const model::ListQueryParams& params = {});

    static std::vector<APIKeyPtr> listAPIKeys(const model::ListQueryParams& params = {});

    static APIKeyPtr getAPIKey(unsigned int keyId);

    static APIKeyPtr getAPIKey(const std::string& keyName);

    static std::shared_ptr<vh::identities::User> getAPIKeyOwner(unsigned int keyId);
};

}
