#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <ctime>
#include <optional>
#include <unordered_map>
#include <nlohmann/json_fwd.hpp>
#include <vector>
#include <memory>
#include <boost/uuid/uuid.hpp>

namespace pqxx {
    class row;
    class result;
}

namespace vh::fs::model {
    struct File;
    struct Directory;

    struct Entry {
        uint32_t id{};
        std::string name{}, base32_alias{};
        uintmax_t size_bytes{0};
        std::optional<int32_t> parent_id{}, owner_uid{}, group_gid{}, vault_id{}, created_by{}, last_modified_by{};
        std::optional<ino_t> inode{};
        std::optional<mode_t> mode{};
        std::filesystem::path path{}, fuse_path{}, backing_path{};
        bool is_hidden{}, is_system{};
        std::time_t created_at{}, updated_at{};

        Entry() = default;

        virtual ~Entry() = default;

        explicit Entry(const std::string &s3_key);

        Entry(const pqxx::row &row, const pqxx::result &parentRows);

        [[nodiscard]] bool operator==(const Entry &other) const;

        [[nodiscard]] virtual bool isDirectory() const = 0;

        void setPath(const std::filesystem::path &path) { this->path = path; }

        void print() const;
    };

    void to_json(nlohmann::json &j, const Entry &entry);

    void from_json(const nlohmann::json &j, Entry &entry);

    void to_json(nlohmann::json &j, const std::vector<std::shared_ptr<Entry> > &entries);

    std::vector<std::shared_ptr<Entry> > merge_entries(const std::vector<std::shared_ptr<File> > &files,
                                                       const std::vector<std::shared_ptr<Directory> > &directories);

    std::vector<std::shared_ptr<Entry> > fromS3XML(const std::u8string &xml);

    std::unordered_map<std::u8string, std::shared_ptr<Entry> > groupEntriesByPath(
        const std::vector<std::shared_ptr<Entry> > &entries);
}
