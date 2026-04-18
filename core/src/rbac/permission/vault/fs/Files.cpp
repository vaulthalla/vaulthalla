#include "rbac/permission/vault/fs/Files.hpp"

#include <nlohmann/json.hpp>
#include <ostream>

namespace vh::rbac::permission::vault::fs {

std::string Files::toFlagsString() const {
    return joinFlagsWithOwn(share);
}

std::string Files::toString(const uint8_t indent) const {
    std::ostringstream oss;
    oss << std::string(indent, ' ') << "Files:\n";
    const auto in = std::string(indent + 2, ' ');
    oss << in << "Preview: " << bool_to_string(canPreview()) << "\n";
    oss << in << "Upload: " << bool_to_string(canUpload()) << "\n";
    oss << in << "Download: " << bool_to_string(canDownload()) << "\n";
    oss << in << "Overwrite: " << bool_to_string(canOverwrite()) << "\n";
    oss << in << "Rename: " << bool_to_string(canRename()) << "\n";
    oss << in << "Delete: " << bool_to_string(canDelete()) << "\n";
    oss << in << "Move: " << bool_to_string(canMove()) << "\n";
    oss << in << "Copy: " << bool_to_string(canCopy()) << "\n";
    oss << in << "Share: " << share.toString(indent + 4);
    return oss.str();
}

void to_json(nlohmann::json& j, const Files& f) {
    j = {
        {"preview", f.canPreview()},
        {"upload", f.canUpload()},
        {"download", f.canDownload()},
        {"overwrite", f.canOverwrite()},
        {"rename", f.canRename()},
        {"delete", f.canDelete()},
        {"move", f.canMove()},
        {"copy", f.canCopy()},
        {"share", f.share}
    };
}

void from_json(const nlohmann::json& j, Files& f) {
    j.at("share").get_to(f.share);
    f.clear();
    if (j.at("preview").get<bool>()) f.grant(FilePermissions::Preview);
    if (j.at("upload").get<bool>()) f.grant(FilePermissions::Upload);
    if (j.at("download").get<bool>()) f.grant(FilePermissions::Download);
    if (j.at("overwrite").get<bool>()) f.grant(FilePermissions::Overwrite);
    if (j.at("rename").get<bool>()) f.grant(FilePermissions::Rename);
    if (j.at("delete").get<bool>()) f.grant(FilePermissions::Delete);
    if (j.at("move").get<bool>()) f.grant(FilePermissions::Move);
    if (j.at("copy").get<bool>()) f.grant(FilePermissions::Copy);
}

}
