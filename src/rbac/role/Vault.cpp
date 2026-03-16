#include "rbac/role/Vault.hpp"
#include "db/encoding/timestamp.hpp"

#include <pqxx/result>
#include <nlohmann/json.hpp>
#include <ostream>

using namespace vh::db::encoding;

namespace vh::rbac::role {

Vault::Vault(const pqxx::row& row, const pqxx::result& overrides)
    : Meta(row),
      Base(row),
      assignment(std::nullopt) {
    if (!row["assignment_id"].is_null()) {
        assignment = AssignmentInfo();
        if (!row["subject_id"].is_null()) assignment->subject_id = row["subject_id"].as<uint32_t>();
        if (!row["vault_id"].is_null()) assignment->vault_id = row["vault_id"].as<uint32_t>();
        if (!row["subject_type"].is_null()) assignment->subject_type = row["subject_type"].as<std::string>();
    }
}

Vault::Vault(const nlohmann::json& j)
    : Meta(j),
      Base(j),
      assignment(std::nullopt) {
    if (j.contains("assignment") && !j["assignment"].is_null()) {
        assignment = AssignmentInfo();
        if (j["assignment"].contains("subject_id") && !j["assignment"]["subject_id"].is_null())
            assignment->subject_id = j["assignment"]["subject_id"].get<uint32_t>();
        if (j["assignment"].contains("vault_id") && !j["assignment"]["vault_id"].is_null())
            assignment->vault_id = j["assignment"]["vault_id"].get<uint32_t>();
        if (j["assignment"].contains("subject_type") && !j["assignment"]["subject_type"].is_null())
            assignment->subject_type = j["assignment"]["subject_type"].get<std::string>();
    }
}

std::string Vault::toFlagsString() const {
    return sync.toFlagsString() + " " +
}

Vault Vault::fromJson(const nlohmann::json& j) { return Vault(j); }

std::string Vault::AssignmentInfo::toString(const uint8_t indent) const {
    std::ostringstream oss;
    oss << std::string(indent, ' ') << "Assignment Info:\n";
    const std::string in(indent + 2, ' ');
    oss << in << "Subject ID: " << subject_id << "\n";
    oss << in << "Vault ID: " << vault_id << "\n";
    oss << in << "Subject Type: " << subject_type << "\n";
    return oss.str();
}

std::string Vault::toString(const uint8_t indent) const {
    std::ostringstream oss;
    oss << std::string(indent, ' ') << "Vault Role:\n";
    const std::string in(indent + 2, ' ');
    if (assignment) oss << in << assignment->toString(indent);
    oss << Base::toString(indent);
    return oss.str();
}

void to_json(nlohmann::json& j, const Vault::AssignmentInfo& r) {
    j = {
        {"subject_id", std::to_string(r.subject_id)},
        {"vault_id", std::to_string(r.vault_id)},
        {"subject_type", r.subject_type}
    };
}

void from_json(const nlohmann::json& j, Vault::AssignmentInfo& r) {
    if (j.contains("subject_id") && !j["subject_id"].is_null()) r.subject_id = j["subject_id"].get<uint32_t>();
    if (j.contains("vault_id") && !j["vault_id"].is_null()) r.vault_id = j["vault_id"].get<uint32_t>();
    if (j.contains("subject_type") && !j["subject_type"].is_null()) r.subject_type = j["subject_type"].get<std::string>();
}

void to_json(nlohmann::json& j, const std::vector<Vault>& roles) {
    j = nlohmann::json::array();
    for (const auto& role : roles) j.emplace_back(role);
}

void to_json(nlohmann::json& j, const std::vector<std::shared_ptr<Vault>>& roles) {
    j = nlohmann::json::array();
    for (const auto& role : roles) j.emplace_back(*role);
}

void to_json(nlohmann::json& j, const Vault& r) {
    j = nlohmann::json::object();

    const nlohmann::json meta  = static_cast<const Meta&>(r);
    const nlohmann::json perms = static_cast<const vault::Base&>(r);

    j.update(meta);
    j.update(perms);

    if (r.assignment) j["assignment"] = *r.assignment;
}

void from_json(const nlohmann::json& j, Vault& r) { r = Vault(j); }

std::unordered_map<uint32_t, std::shared_ptr<Vault>> vault_roles_from_pq_result(const pqxx::result& res, const pqxx::result& overrides) {
    std::unordered_map<uint32_t, std::shared_ptr<Vault>> roles;
    for (const auto& row : res) {
        // TODO: Either split the overrides by vault id or share a central map pointer
        const auto role = std::make_shared<Vault>(row, overrides);
        roles[role->id] = role;
    }
    return roles;
}

std::vector<Vault> vault_roles_from_json(const nlohmann::json& j) {
    std::vector<Vault> roles;
    if (j.is_array()) for (const auto& item : j) roles.emplace_back(item);
    else if (j.is_object()) roles.emplace_back(j);
    return roles;
}

void to_json(nlohmann::json& j, const std::unordered_map<uint32_t, std::shared_ptr<Vault> >& roles) {
    j = nlohmann::json::array();
    for (const auto& [id, role] : roles) j.emplace_back(*role);
}

}
