#include "protocols/ws/Handler.hpp"
#include "protocols/ws/Router.hpp"
#include "protocols/ws/handler/Auth.hpp"
#include "protocols/ws/handler/Storage.hpp"
#include "protocols/ws/handler/Permissions.hpp"
#include "protocols/ws/handler/Settings.hpp"
#include "protocols/ws/handler/Groups.hpp"
#include "protocols/ws/handler/Stats.hpp"
#include "protocols/ws/handler/Roles.hpp"
#include "protocols/ws/handler/Vaults.hpp"
#include "protocols/ws/handler/APIKeys.hpp"
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
    r->registerPayload("fs.upload.start", &Storage::startUpload);
    r->registerPayload("fs.upload.finish", &Storage::finishUpload);
    r->registerPayload("fs.dir.create", &Storage::mkdir);
    r->registerPayload("fs.dir.list", &Storage::listDir);
    r->registerPayload("fs.entry.delete", &Storage::remove);
    r->registerPayload("fs.entry.move", &Storage::move);
    r->registerPayload("fs.entry.rename", &Storage::rename);
    r->registerPayload("fs.entry.copy", &Storage::copy);
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
    r->registerPayload("role.add", &Roles::add);
    r->registerPayload("role.update", &Roles::update);
    r->registerPayload("role.delete", &Roles::remove);
    r->registerPayload("role.get", &Roles::get);
    r->registerPayload("role.get.byName", &Roles::getByName);
    r->registerSessionOnlyHandler("roles.list", &Roles::list);
    r->registerSessionOnlyHandler("roles.list.user", &Roles::listUserRoles);
    r->registerSessionOnlyHandler("roles.list.vault", &Roles::listVaultRoles);
}

void Handler::registerPermissionsHandlers(const std::shared_ptr<Router>& r) {
    r->registerPayload("permission.get", &Permissions::get);
    r->registerPayload("permission.get.byName", &Permissions::getByName);
    r->registerSessionOnlyHandler("permissions.list", &Permissions::list);
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
