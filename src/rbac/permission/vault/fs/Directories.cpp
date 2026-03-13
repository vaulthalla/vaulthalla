#include "rbac/permission/vault/fs/Directories.hpp"

#include <nlohmann/json.hpp>

namespace vh::rbac::permission::vault::fs {

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
