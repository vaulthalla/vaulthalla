#include "rbac/permission/vault/fs/Files.hpp"

#include <nlohmann/json.hpp>

namespace vh::rbac::permission::vault::fs {

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
