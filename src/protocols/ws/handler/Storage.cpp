#include "protocols/ws/handler/Storage.hpp"
#include "protocols/ws/Session.hpp"
#include "storage/Manager.hpp"
#include "fs/model/File.hpp"
#include "fs/model/Path.hpp"
#include "rbac/model/VaultRole.hpp"
#include "vault/model/Vault.hpp"
#include "identities/model/User.hpp"
#include "protocols/ws/handler/Upload.hpp"
#include "storage/Engine.hpp"
#include "runtime/Deps.hpp"
#include "fs/Filesystem.hpp"
#include "sync/Controller.hpp"
#include "fs/cache/Registry.hpp"

using namespace vh::protocols::ws::handler;
using namespace vh::vault::model;
using namespace vh::rbac::model;
using namespace vh::storage;
using namespace vh::fs;
using namespace vh::fs::model;

json Storage::startUpload(const json& payload, const std::shared_ptr<Session>& session) {
    const auto vaultId = payload.at("vault_id").get<unsigned int>();
    const auto path = payload.at("path").get<std::string>();

    enforcePermissions(session, vaultId, path, &VaultRole::canCreate);

    const auto engine = runtime::Deps::get().storageManager->getEngine(vaultId);
    if (!engine) throw std::runtime_error("Unknown storage engine");

    const auto uploadId = Session::generateUUIDv4();
    const auto absPath = engine->paths->absPath(path, PathType::VAULT_ROOT);
    const auto tmpPath = absPath.parent_path() / (".upload-" + uploadId + ".part");

    session->getUploadHandler()->startUpload( {
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
    const auto vaultId = payload.at("vault_id").get<unsigned int>();
    const auto path = payload.at("path").get<std::string>();
    enforcePermissions(session, vaultId, path, &VaultRole::canCreate);
    session->getUploadHandler()->finishUpload();
    return {{"path", path}};
}

json Storage::mkdir(const json& payload, const std::shared_ptr<Session>& session) {
    const auto vaultId = payload.at("vault_id").get<unsigned int>();
    const auto path = payload.at("path").get<std::string>();

    enforcePermissions(session, vaultId, path, &VaultRole::canCreate);

    const auto engine = runtime::Deps::get().storageManager->getEngine(vaultId);
    if (!engine) throw std::runtime_error("No storage engine found for vault with ID: " + std::to_string(vaultId));
    engine->mkdir(path, session->user->id);
    runtime::Deps::get().syncController->runNow(vaultId);

    return {{"path", path}};
}

json Storage::move(const json& payload, const std::shared_ptr<Session>& session) {
    const auto vaultId = payload.at("vault_id").get<unsigned int>();
    const auto from = payload.at("from").get<std::string>();
    const auto to = payload.at("to").get<std::string>();

    enforcePermissions(session, vaultId, from, &VaultRole::canMove);
    enforcePermissions(session, vaultId, to, &VaultRole::canCreate);

    const auto engine = runtime::Deps::get().storageManager->getEngine(vaultId);
    if (!engine) throw std::runtime_error("No storage engine found for vault with ID: " + std::to_string(vaultId));
    engine->move(from, to, session->user->id);
    runtime::Deps::get().syncController->runNow(vaultId);

    return {{"from", from}, {"to", to}};
}

json Storage::rename(const json& payload, const std::shared_ptr<Session>& session) {
    const auto vaultId = payload.at("vault_id").get<unsigned int>();
    const auto from = std::filesystem::path(payload.at("from").get<std::string>());
    const auto to = std::filesystem::path(payload.at("to").get<std::string>());

    enforcePermissions(session, vaultId, from, &VaultRole::canRename);
    enforcePermissions(session, vaultId, to, &VaultRole::canCreate);

    const auto engine = runtime::Deps::get().storageManager->getEngine(vaultId);
    if (!engine) throw std::runtime_error("No storage engine found for vault with ID: " + std::to_string(vaultId));
    engine->rename(from, to, session->user->id);
    runtime::Deps::get().syncController->runNow(vaultId);

    return {{"from", from}, {"to", to}};
}

json Storage::copy(const json& payload, const std::shared_ptr<Session>& session) {
    const auto vaultId = payload.at("vault_id").get<unsigned int>();
    const auto from = std::filesystem::path(payload.at("from").get<std::string>());
    const auto to = std::filesystem::path(payload.at("to").get<std::string>());

    enforcePermissions(session, vaultId, from, &VaultRole::canMove);
    enforcePermissions(session, vaultId, to, &VaultRole::canCreate);

    const auto engine = runtime::Deps::get().storageManager->getEngine(vaultId);
    if (!engine) throw std::runtime_error("No storage engine found for vault with ID: " + std::to_string(vaultId));
    engine->copy(from, to, session->user->id);
    runtime::Deps::get().syncController->runNow(vaultId);

    return {{"from", from}, {"to", to}};
}

json Storage::listDir(const json& payload, const std::shared_ptr<Session>& session) {
    const auto vaultId = payload.at("vault_id").get<unsigned int>();
    auto path = std::filesystem::path(payload.value("path", "/"));
    if (path.empty()) path = std::filesystem::path("/");

    enforcePermissions(session, vaultId, path, &VaultRole::canList);

    const auto engine = runtime::Deps::get().storageManager->getEngine(vaultId);
    if (!engine) throw std::runtime_error("No storage engine found for vault with ID: " + std::to_string(vaultId));

    const auto fusePath = engine->paths->absRelToAbsRel(path, PathType::VAULT_ROOT, PathType::FUSE_ROOT);
    if (!Filesystem::exists(fusePath)) throw std::runtime_error("Path does not exist: " + fusePath.string());

    const auto entry = runtime::Deps::get().fsCache->getEntry(fusePath);
    if (!entry) throw std::runtime_error("No entry found for path: " + fusePath.string());

    const auto entries = runtime::Deps::get().fsCache->listDir(entry->id, false);

    return {
                {"vault", engine->vault->name},
                {"path", path},
                {"files", entries}
    };
}

json Storage::remove(const json& payload, const std::shared_ptr<Session>& session) {
    const auto user = session->user;
    if (!user) throw std::runtime_error("User not authenticated");

    const auto userId = user->id;
    if (userId == 0) throw std::runtime_error("User not authenticated");

    const auto vaultId = payload.at("vault_id").get<unsigned int>();
    const auto path = std::filesystem::path(payload.at("path").get<std::string>());

    enforcePermissions(session, vaultId, path, &VaultRole::canDelete);

    const auto engine = runtime::Deps::get().storageManager->getEngine(vaultId);
    if (!engine) throw std::runtime_error("No storage engine found for vault with ID: " + std::to_string(vaultId));
    engine->remove(path, userId);
    runtime::Deps::get().syncController->runNow(vaultId);

    return {{"path", path.string()}};
}
