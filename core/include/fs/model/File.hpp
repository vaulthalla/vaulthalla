#pragma once

#include "Entry.hpp"

namespace vh::fs::model {
    struct File final : Entry {
        std::string encryption_iv;
        std::optional<std::string> mime_type, content_hash;
        unsigned int encrypted_with_key_version{};

        File() = default;

        File(const pqxx::row &row, const pqxx::result &parentRows);

        File(const std::string &s3_key, uint64_t size, const std::optional<std::time_t> &updated = {});

        [[nodiscard]] bool isDirectory() const override { return false; }

        [[nodiscard]] bool operator==(const File &other) const;
    };

    void to_json(nlohmann::json &j, const File &f);

    void from_json(const nlohmann::json &j, File &f);

    void to_json(nlohmann::json &j, const std::vector<std::shared_ptr<File> > &files);

    std::vector<std::shared_ptr<File> > files_from_pq_res(const pqxx::result &res);

    std::vector<std::shared_ptr<File> > filesFromS3XML(const std::u8string &xml);

    std::unordered_map<std::u8string, std::shared_ptr<File> > groupEntriesByPath(
        const std::vector<std::shared_ptr<File> > &entries);
}
