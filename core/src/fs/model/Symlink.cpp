#include "fs/model/Symlink.hpp"
#include "db/query/fs/Entry.hpp"

#include <nlohmann/json.hpp>
#include <pqxx/result>

using namespace vh::fs::model;

Symlink::Symlink(const pqxx::row& row, const pqxx::result& parentRows)
    : Entry(row, parentRows),
      target(row["target"].as<std::string>()) {
    size_bytes = target.size();
}

void vh::fs::model::to_json(nlohmann::json& j, const Symlink& s) {
    to_json(j, static_cast<const Entry&>(s));
    j["type"] = "symlink";
    j["target"] = s.target;
}

void vh::fs::model::from_json(const nlohmann::json& j, Symlink& s) {
    from_json(j, static_cast<Entry&>(s));
    s.target = j.at("target").get<std::string>();
    s.size_bytes = s.target.size();
}

void vh::fs::model::to_json(nlohmann::json& j, const std::vector<std::shared_ptr<Symlink>>& symlinks) {
    j = nlohmann::json::array();
    for (const auto& symlink : symlinks) j.push_back(*symlink);
}

std::vector<std::shared_ptr<Symlink>> vh::fs::model::symlinks_from_pq_res(const pqxx::result& res) {
    std::vector<std::shared_ptr<Symlink>> symlinks;
    for (const auto& row : res) {
        if (const auto parentId = row["parent_id"].as<std::optional<unsigned int>>()) {
            const auto parentChain = db::query::fs::Entry::collectParentChain(*parentId);
            symlinks.push_back(std::make_shared<Symlink>(row, parentChain));
        } else symlinks.push_back(std::make_shared<Symlink>(row, pqxx::result{}));
    }
    return symlinks;
}
