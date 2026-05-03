#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace vh::db::model { struct ListQueryParams; }
namespace vh::share { struct AuditEvent; }

namespace vh::db::query::share {

struct AuditEvent {
    static void append(const std::shared_ptr<vh::share::AuditEvent>& event);
    static std::vector<std::shared_ptr<vh::share::AuditEvent>> listForShare(const std::string& share_id, const db::model::ListQueryParams& params);
    static std::vector<std::shared_ptr<vh::share::AuditEvent>> listForVault(uint32_t vault_id, const db::model::ListQueryParams& params);
};

}
