#include "rbac/permission/vault/fs/Directories.hpp"

#include <nlohmann/json.hpp>
#include <ostream>

namespace vh::rbac::permission::vault::fs {

std::string Directories::toFlagsString() const {
    return joinFlagsWithOwn(share);
}


std::string Directories::toString(const uint8_t indent) const {
    std::ostringstream oss;
    oss << std::string(indent, ' ') << "Directories:\n";
    const auto in = std::string(indent + 2, ' ');
    oss << in << "List: " << bool_to_string(canList()) << "\n";
    oss << in << "Upload: " << bool_to_string(canUpload()) << "\n";
    oss << in << "Download: " << bool_to_string(canDownload()) << "\n";
    oss << in << "Touch: " << bool_to_string(canTouch()) << "\n";
    oss << in << "Delete: " << bool_to_string(canDelete()) << "\n";
    oss << in << "Rename: " << bool_to_string(canRename()) << "\n";
    oss << in << "Copy: " << bool_to_string(canCopy()) << "\n";
    oss << in << "Move: " << bool_to_string(canMove()) << "\n";
    oss << in << "Share: " << share.toString(indent + 4);
    return oss.str();
}

void to_json(nlohmann::json& j, const Directories& v) {
    j = {
        {"list", v.canList()},
        {"upload", v.canUpload()},
        {"download", v.canDownload()},
        {"touch", v.canTouch()},
        {"delete", v.canDelete()},
        {"rename", v.canRename()},
        {"copy", v.canCopy()},
        {"move", v.canMove()},
        {"share", v.share}
    };
}

void from_json(const nlohmann::json& j, Directories& v) {
    v.share = j.at("share").get<Share>();
    v.clear();
    if (j.at("list").get<bool>()) v.grant(DirectoryPermissions::List);
    if (j.at("upload").get<bool>()) v.grant(DirectoryPermissions::Upload);
    if (j.at("download").get<bool>()) v.grant(DirectoryPermissions::Download);
    if (j.at("touch").get<bool>()) v.grant(DirectoryPermissions::Touch);
    if (j.at("delete").get<bool>()) v.grant(DirectoryPermissions::Delete);
    if (j.at("rename").get<bool>()) v.grant(DirectoryPermissions::Rename);
    if (j.at("copy").get<bool>()) v.grant(DirectoryPermissions::Copy);
    if (j.at("move").get<bool>()) v.grant(DirectoryPermissions::Move);
}

}
