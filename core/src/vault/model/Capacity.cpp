#include "vault/model/Capacity.hpp"
#include "db/query/fs/Directory.hpp"
#include "fs/model/File.hpp"
#include "fs/model/Directory.hpp"
#include "vault/model/Vault.hpp"
#include "storage/Engine.hpp"
#include "storage/Manager.hpp"
#include "db/query/fs/File.hpp"
#include "runtime/Deps.hpp"
#include "fs/model/stats/Extension.hpp"

#include <nlohmann/json.hpp>

using namespace vh::vault::model;

struct CompareFiles {
    bool operator()(const std::pair<std::string, unsigned int>& a, const std::pair<std::string, unsigned int>& b) const {
        return a.second < b.second;
    }
};

static std::array<std::pair<std::string, uintmax_t>, 10>
toTop10Array(const std::vector<vh::fs::model::stats::Extension>& v) {
    std::array<std::pair<std::string, uintmax_t>, 10> a{};
    for (size_t i = 0; i < a.size() && i < v.size(); ++i) {
        a[i] = { v[i].extension, static_cast<uintmax_t>(v[i].total_bytes) };
    }
    return a;
}

static nlohmann::json top10ToDict(const std::array<std::pair<std::string, uintmax_t>, 10>& arr) {
    nlohmann::json j;
    for (const auto & [k, v] : arr) j[k] = v;
    return j;
}

Capacity::Capacity(const unsigned int vaultId) {
    const auto engine = runtime::Deps::get().storageManager->getEngine(vaultId);
    if (!engine) throw std::runtime_error("vaultId not available");

    const auto dir = db::query::fs::Directory::getDirectoryByPath(vaultId, "/");
    if (!dir) throw std::runtime_error("vaultId not found");

    capacity = engine->vault->quota;
    logical_size = dir->size_bytes;
    physical_size = engine->getVaultSize();
    cache_size = engine->getCacheSize();
    free_space = engine->freeSpace();
    file_count = dir->file_count;
    directory_count = dir->subdirectory_count;
    average_file_size_bytes = file_count > 0 ? dir->size_bytes / file_count : 0;
    if (const auto largest = db::query::fs::File::getLargestFile(engine->vault->id))
        largest_file_size_bytes = largest->size_bytes;
    top_file_extensions_by_size =
        toTop10Array(db::query::fs::File::getTopExtensionsBySize(engine->vault->id, 10));
}

void vh::vault::model::to_json(nlohmann::json& j, const std::shared_ptr<Capacity>& stats) {
    j["capacity"] = stats->capacity;
    j["logical_size"] = stats->logical_size;
    j["physical_size"] = stats->physical_size;
    j["free_space"] = stats->free_space;
    j["cache_size"] = stats->cache_size;
    j["file_count"] = stats->file_count;
    j["directory_count"] = stats->directory_count;
    j["average_file_size"] = stats->average_file_size_bytes;
    j["largest_file_size"] = stats->largest_file_size_bytes;
    j["top_file_extensions"] = top10ToDict(stats->top_file_extensions_by_size);
}
