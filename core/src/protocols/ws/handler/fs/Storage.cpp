#include "protocols/ws/handler/fs/Storage.hpp"
#include "protocols/ws/Session.hpp"
#include "storage/Manager.hpp"
#include "fs/model/File.hpp"
#include "fs/model/Path.hpp"
#include "rbac/role/Vault.hpp"
#include "vault/model/Vault.hpp"
#include "identities/User.hpp"
#include "protocols/ws/handler/fs/Upload.hpp"
#include "storage/Engine.hpp"
#include "runtime/Deps.hpp"
#include "fs/Filesystem.hpp"
#include "sync/Controller.hpp"
#include "fs/cache/Registry.hpp"

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>

using namespace vh::protocols::ws::handler::fs;
using namespace vh::vault::model;
using namespace vh::rbac;
using namespace vh::storage;
using namespace vh::fs;
using namespace vh::fs::model;

json Storage::startUpload(const json& payload, const std::shared_ptr<Session>& session) {
    const auto vaultId = payload.at("vault_id").get<uint32_t>();
    const auto path = std::filesystem::path(payload.at("path").get<std::string>());

    const auto engine = runtime::Deps::get().storageManager->getEngine(vaultId);
    if (!engine) throw std::runtime_error("Unknown storage engine");

    const auto uploadId = Session::generateUUIDv4();
    const auto absPath = engine->paths->absPath(path, PathType::VAULT_ROOT);
    const auto tmpPath = absPath.parent_path() / (".upload-" + uploadId + ".part");

    enforcePermission(
        session,
        engine,
        path,
        permission::vault::FilesystemAction::Write,
        "Permission denied: User does not have upload permission for this path in the vault"
    );

    session->getUploadHandler()->startUpload({
        .uploadId = uploadId,
        .expectedSize = payload.at("size").get<uint64_t>(),
        .engine = engine,
        .tmpPath = tmpPath,
        .finalPath = absPath,
        .fuseFrom = engine->paths->absRelToRoot(tmpPath, PathType::FUSE_ROOT),
        .fuseTo = engine->paths->absRelToRoot(absPath, PathType::FUSE_ROOT)
    });

    return {{"upload_id", uploadId}};
}

json Storage::finishUpload(const json& payload, const std::shared_ptr<Session>& session) {
    const auto vaultId = payload.at("vault_id").get<uint32_t>();
    const auto path = std::filesystem::path(payload.at("path").get<std::string>());

    const auto engine = runtime::Deps::get().storageManager->getEngine(vaultId);
    if (!engine) throw std::runtime_error("Unknown storage engine");

    enforcePermission(
        session,
        engine,
        path,
        permission::vault::FilesystemAction::Write,
        "Permission denied: User does not have upload permission for this path in the vault"
    );

    session->getUploadHandler()->finishUpload();
    return {{"path", path.string()}};
}

json Storage::mkdir(const json& payload, const std::shared_ptr<Session>& session) {
    const auto vaultId = payload.at("vault_id").get<uint32_t>();
    const auto path = std::filesystem::path(payload.at("path").get<std::string>());

    const auto engine = runtime::Deps::get().storageManager->getEngine(vaultId);
    if (!engine) throw std::runtime_error("No storage engine found for vault with ID: " + std::to_string(vaultId));

    enforcePermission(
        session,
        engine,
        path,
        permission::vault::FilesystemAction::Touch,
        "Permission denied: User does not have create-directory permission for this path in the vault"
    );

    engine->mkdir(path, session->user->id);
    runtime::Deps::get().syncController->runNow(vaultId);

    return {{"path", path.string()}};
}

json Storage::move(const json& payload, const std::shared_ptr<Session>& session) {
    const auto vaultId = payload.at("vault_id").get<uint32_t>();
    const auto from = std::filesystem::path(payload.at("from").get<std::string>());
    const auto to = std::filesystem::path(payload.at("to").get<std::string>());

    const auto engine = runtime::Deps::get().storageManager->getEngine(vaultId);
    if (!engine) throw std::runtime_error("No storage engine found for vault with ID: " + std::to_string(vaultId));

    enforcePermission(
        session,
        engine,
        from,
        permission::vault::FilesystemAction::Move,
        "Permission denied: User does not have move permission for the source path in the vault"
    );

    engine->move(from, to, session->user->id);
    runtime::Deps::get().syncController->runNow(vaultId);

    return {
        {"from", from.string()},
        {"to", to.string()}
    };
}

json Storage::rename(const json& payload, const std::shared_ptr<Session>& session) {
    const auto vaultId = payload.at("vault_id").get<uint32_t>();
    const auto from = std::filesystem::path(payload.at("from").get<std::string>());
    const auto to = std::filesystem::path(payload.at("to").get<std::string>());

    const auto engine = runtime::Deps::get().storageManager->getEngine(vaultId);
    if (!engine) throw std::runtime_error("No storage engine found for vault with ID: " + std::to_string(vaultId));

    enforcePermission(
        session,
        engine,
        from,
        permission::vault::FilesystemAction::Rename,
        "Permission denied: User does not have rename permission for the source path in the vault"
    );

    engine->rename(from, to, session->user->id);
    runtime::Deps::get().syncController->runNow(vaultId);

    return {
        {"from", from.string()},
        {"to", to.string()}
    };
}

json Storage::copy(const json& payload, const std::shared_ptr<Session>& session) {
    const auto vaultId = payload.at("vault_id").get<uint32_t>();
    const auto from = std::filesystem::path(payload.at("from").get<std::string>());
    const auto to = std::filesystem::path(payload.at("to").get<std::string>());

    const auto engine = runtime::Deps::get().storageManager->getEngine(vaultId);
    if (!engine) throw std::runtime_error("No storage engine found for vault with ID: " + std::to_string(vaultId));

    enforcePermission(
        session,
        engine,
        from,
        permission::vault::FilesystemAction::Copy,
        "Permission denied: User does not have read permission for the source path in the vault"
    );

    engine->copy(from, to, session->user->id);
    runtime::Deps::get().syncController->runNow(vaultId);

    return {
        {"from", from.string()},
        {"to", to.string()}
    };
}

json Storage::listDir(const json& payload, const std::shared_ptr<Session>& session) {
    const auto vaultId = payload.at("vault_id").get<uint32_t>();
    auto path = std::filesystem::path(payload.value("path", "/"));
    if (path.empty()) path = std::filesystem::path("/");

    const auto engine = runtime::Deps::get().storageManager->getEngine(vaultId);
    if (!engine) throw std::runtime_error("No storage engine found for vault with ID: " + std::to_string(vaultId));

    enforcePermission(
        session,
        engine,
        path,
        permission::vault::FilesystemAction::List,
        "Permission denied: User does not have list permission for this path in the vault"
    );

    const auto fusePath = engine->paths->absRelToAbsRel(path, PathType::VAULT_ROOT, PathType::FUSE_ROOT);
    if (!Filesystem::exists(fusePath)) throw std::runtime_error("Path does not exist: " + fusePath.string());

    const auto entry = runtime::Deps::get().fsCache->getEntry(fusePath);
    if (!entry) throw std::runtime_error("No entry found for path: " + fusePath.string());

    const auto entries = runtime::Deps::get().fsCache->listDir(entry->id, false);

    return {
        {"vault", engine->vault->name},
        {"path", path.string()},
        {"entry", *entry},
        {"files", entries}
    };
}

json Storage::remove(const json& payload, const std::shared_ptr<Session>& session) {
    const auto user = session->user;
    if (!user) throw std::runtime_error("User not authenticated");

    const auto userId = user->id;
    if (userId == 0) throw std::runtime_error("User not authenticated");

    const auto vaultId = payload.at("vault_id").get<uint32_t>();
    const auto path = std::filesystem::path(payload.at("path").get<std::string>());

    const auto engine = runtime::Deps::get().storageManager->getEngine(vaultId);
    if (!engine) throw std::runtime_error("No storage engine found for vault with ID: " + std::to_string(vaultId));

    enforcePermission(
        session,
        engine,
        path,
        permission::vault::FilesystemAction::Delete,
        "Permission denied: User does not have delete permission for this path in the vault"
    );

    engine->remove(path, userId);
    runtime::Deps::get().syncController->runNow(vaultId);

    return {{"path", path.string()}};
}
