#include "protocols/ws/Handler.hpp"
#include "protocols/ws/Router.hpp"
#include "protocols/ws/handler/Auth.hpp"
#include "protocols/ws/handler/fs/Storage.hpp"
#include "protocols/ws/handler/rbac/Permissions.hpp"
#include "protocols/ws/handler/Settings.hpp"
#include "protocols/ws/handler/Groups.hpp"
#include "protocols/ws/handler/Stats.hpp"
#include "protocols/ws/handler/rbac/roles/Admin.hpp"
#include "protocols/ws/handler/rbac/roles/Vault.hpp"
#include "protocols/ws/handler/vault/Vaults.hpp"
#include "protocols/ws/handler/vault/APIKeys.hpp"
#include "log/Registry.hpp"

using namespace vh::protocols::ws;
using namespace vh::protocols::ws::handler;

void Handler::registerAllHandlers(const std::shared_ptr<Router>& r) {
    registerAuthHandlers(r);
    registerFileSystemHandlers(r);
    registerStorageHandlers(r);
    registerAPIKeyHandlers(r);
    registerRoleHandlers(r);
    registerPermissionsHandlers(r);
    registerSettingsHandlers(r);
    registerGroupHandlers(r);
    registerStatHandlers(r);

    log::Registry::ws()->debug("[WebSocketHandler] All handlers registered successfully.");
}

void Handler::registerAuthHandlers(const std::shared_ptr<Router>& r) {
    r->registerPayload("auth.login", &Auth::login);
    r->registerPayload("auth.register", &Auth::registerUser);
    r->registerPayload("auth.user.delete", &Auth::deleteUser);
    r->registerSessionOnlyHandler("auth.logout", &Auth::logout);
    r->registerPayload("auth.user.update", &Auth::updateUser);
    r->registerPayload("auth.user.change_password", &Auth::changePassword);
    r->registerHandlerWithToken("auth.isAuthenticated", &Auth::isUserAuthenticated);
    r->registerPayload("auth.user.get", &Auth::getUser);
    r->registerPayload("auth.user.get.byName", &Auth::getUserByName);
    r->registerSessionOnlyHandler("auth.users.list", &Auth::listUsers);
    r->registerEmptyHandler("auth.admin.default_password", &Auth::doesAdminHaveDefaultPassword);
    r->registerHandlerWithToken("auth.refresh", &Auth::refreshToken);
}

void Handler::registerFileSystemHandlers(const std::shared_ptr<Router>& r) {
    r->registerPayload("fs.upload.start", &handler::fs::Storage::startUpload);
    r->registerPayload("fs.upload.finish", &handler::fs::Storage::finishUpload);
    r->registerPayload("fs.dir.create", &handler::fs::Storage::mkdir);
    r->registerPayload("fs.dir.list", &handler::fs::Storage::listDir);
    r->registerPayload("fs.entry.delete", &handler::fs::Storage::remove);
    r->registerPayload("fs.entry.move", &handler::fs::Storage::move);
    r->registerPayload("fs.entry.rename", &handler::fs::Storage::rename);
    r->registerPayload("fs.entry.copy", &handler::fs::Storage::copy);
}

void Handler::registerStorageHandlers(const std::shared_ptr<Router>& r) {
    r->registerSessionOnlyHandler("storage.vault.list", &Vaults::list);
    r->registerPayload("storage.vault.add", &Vaults::add);
    r->registerPayload("storage.vault.update", &Vaults::update);
    r->registerPayload("storage.vault.remove", &Vaults::remove);
    r->registerPayload("storage.vault.get", &Vaults::get);
    r->registerPayload("storage.vault.sync", &Vaults::sync);
}

void Handler::registerAPIKeyHandlers(const std::shared_ptr<Router>& r) {
    r->registerPayload("storage.apiKey.add", &APIKeys::add);
    r->registerPayload("storage.apiKey.remove", &APIKeys::remove);
    r->registerSessionOnlyHandler("storage.apiKey.list", &APIKeys::list);
    r->registerPayload("storage.apiKey.get", &APIKeys::get);
}

void Handler::registerRoleHandlers(const std::shared_ptr<Router>& r) {
    r->registerPayload("role.admin.add", &handler::rbac::roles::Admin::add);
    r->registerPayload("role.admin.update", &handler::rbac::roles::Admin::update);
    r->registerPayload("role.admin.delete", &handler::rbac::roles::Admin::remove);
    r->registerPayload("role.admin.get", &handler::rbac::roles::Admin::get);
    r->registerPayload("role.admin.get.byName", &handler::rbac::roles::Admin::getByName);
    r->registerSessionOnlyHandler("roles.admin.list", &handler::rbac::roles::Admin::list);

    r->registerPayload("role.vault.add", &handler::rbac::roles::Vault::add);
    r->registerPayload("role.vault.update", &handler::rbac::roles::Vault::update);
    r->registerPayload("role.vault.delete", &handler::rbac::roles::Vault::remove);
    r->registerPayload("role.vault.get", &handler::rbac::roles::Vault::get);
    r->registerPayload("role.vault.get.byName", &handler::rbac::roles::Vault::getByName);
    r->registerSessionOnlyHandler("roles.vault.list", &handler::rbac::roles::Vault::list);
}

void Handler::registerPermissionsHandlers(const std::shared_ptr<Router>& r) {
    r->registerPayloadOnly("permission.get", &handler::rbac::Permissions::get);
    r->registerPayloadOnly("permission.get.byName", &handler::rbac::Permissions::getByName);
    r->registerEmptyHandler("permissions.list", &handler::rbac::Permissions::list);
}

void Handler::registerSettingsHandlers(const std::shared_ptr<Router>& r) {
    r->registerSessionOnlyHandler("settings.get", &Settings::get);
    r->registerPayload("settings.update", &Settings::update);
}

void Handler::registerGroupHandlers(const std::shared_ptr<Router>& r) {
    r->registerPayload("group.add", &Groups::add);
    r->registerPayload("group.update", &Groups::update);
    r->registerPayload("group.remove", &Groups::remove);
    r->registerPayload("group.member.add", &Groups::addMember);
    r->registerPayload("group.member.remove", &Groups::removeMember);
    r->registerPayload("group.get", &Groups::get);
    r->registerPayload("group.get.byName", &Groups::getByName);
    r->registerPayload("groups.list.byUser", &Groups::listByUser);
    r->registerSessionOnlyHandler("groups.list", &Groups::list);
}

void Handler::registerStatHandlers(const std::shared_ptr<Router>& r) {
    r->registerPayload("stats.vault", &Stats::vault);
    r->registerSessionOnlyHandler("stats.fs.cache", &Stats::fsCache);
    r->registerSessionOnlyHandler("stats.http.cache", &Stats::httpCache);
}
