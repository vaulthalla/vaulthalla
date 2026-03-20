#pragma once

#include <filesystem>
#include <fuse_lowlevel.h>
#include <memory>
#include <optional>

namespace vh {
    namespace identities { struct User; struct Group; }
    namespace fs::model { struct Entry; }
    namespace storage { struct Engine; }

    namespace fuse::resolver {
        enum class Status {
            Ok,
            MissingFuseContext,
            MissingIno,
            MissingChildName,
            MissingParentPath,
            MissingUser,
            MissingEntry,
            MissingParentIno,
            MissingParentEntry,
            MissingPath,
            MissingEngine,
            InvalidChildPath,
            AccessDenied,
            InvalidRequest,
            InternalError
        };

        struct Resolved {
            Status status {Status::Ok};
            int errnum {0};

            std::shared_ptr<identities::User> user{};
            std::shared_ptr<identities::Group> group{};
            std::shared_ptr<fs::model::Entry> entry{};
            std::optional<std::filesystem::path> path{}, vaultPath{};
            std::optional<fuse_ino_t> ino{};
            std::shared_ptr<storage::Engine> engine{};

            [[nodiscard]] bool ok() const {
                return status == Status::Ok;
            }

            void setStatus(const Status& status, const int errnum) {
                this->status = status;
                this->errnum = errnum;
            }
        };
    }
}
