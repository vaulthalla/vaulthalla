#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace vh::db::model { struct ListQueryParams; }
namespace vh::share { struct Link; }

namespace vh::db::query::share {

struct Link {
    static std::shared_ptr<vh::share::Link> create(const std::shared_ptr<vh::share::Link>& link);
    static std::shared_ptr<vh::share::Link> get(const std::string& id);
    static std::shared_ptr<vh::share::Link> getByLookupId(const std::string& lookup_id);
    static std::vector<std::shared_ptr<vh::share::Link>> listForUser(uint32_t user_id, const db::model::ListQueryParams& params);
    static std::vector<std::shared_ptr<vh::share::Link>> listForVault(uint32_t vault_id, const db::model::ListQueryParams& params);
    static std::vector<std::shared_ptr<vh::share::Link>> listForTarget(uint32_t root_entry_id, const db::model::ListQueryParams& params);
    static std::shared_ptr<vh::share::Link> update(const std::shared_ptr<vh::share::Link>& link);
    static void revoke(const std::string& id, uint32_t revoked_by);
    static void rotateToken(const std::string& id, const std::string& lookup_id, const std::vector<uint8_t>& token_hash, uint32_t updated_by);
    static void touchAccess(const std::string& id);
    static void incrementDownload(const std::string& id);
    static void incrementUpload(const std::string& id);
};

}
