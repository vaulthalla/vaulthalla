#pragma once

#include <cstdint>
#include <ctime>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace pqxx { class row; }

namespace vh::fs::model { struct File; }

namespace vh::sync::model {

struct RemoteObject {
    uint32_t id{};
    uint32_t vault_id{};
    std::filesystem::path object_key;
    uint64_t size_bytes{};
    std::time_t last_modified{};
    std::optional<std::string> etag;
    std::optional<std::string> storage_class;
    std::optional<std::string> restore_status;
    std::string source{"list_objects_v2"};

    RemoteObject() = default;
    explicit RemoteObject(const pqxx::row& row);
    explicit RemoteObject(uint32_t vaultId, const std::shared_ptr<fs::model::File>& file, std::string source = "list_objects_v2");

    [[nodiscard]] std::shared_ptr<fs::model::File> toFile() const;
};

}
