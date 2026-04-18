#pragma once

#include "db/model/ListQueryParams.hpp"

#include <memory>
#include <string>
#include <vector>

namespace vh::rbac::permission { struct Override; }

namespace vh::db::query::rbac::permission {

    class Override {
        using OverrideT = vh::rbac::permission::Override;
        using OverridePtr = std::shared_ptr<OverrideT>;

    public:
        struct Query {
            unsigned int vault_id{};
            std::string subject_type;
            unsigned int subject_id{};
            unsigned int bit_position{};
        };

        static unsigned int upsert(const OverridePtr& permOverride);
        static unsigned int add(const OverridePtr& permOverride);
        static void update(const OverridePtr& permOverride);

        static void remove(unsigned int permOverrideId);
        static void removeByAssignment(unsigned int assignmentId);

        static OverridePtr get(unsigned int permOverrideId);
        static OverridePtr get(const Query& query);

        static bool exists(unsigned int assignmentId, unsigned int permissionId, const std::string& globPath);

        static std::vector<OverridePtr> list(unsigned int vaultId, model::ListQueryParams&& params = {});
        static std::vector<OverridePtr> listAssigned(unsigned int assignmentId, model::ListQueryParams&& params = {});
        static std::vector<OverridePtr> listForSubject(
            const std::string& subjectType,
            unsigned int subjectId,
            model::ListQueryParams&& params = {}
        );
    };

}
