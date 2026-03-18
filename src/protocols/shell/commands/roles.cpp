// #include "protocols/shell/commands/all.hpp"
// #include "protocols/shell/commands/helpers.hpp"
// #include "protocols/shell/Router.hpp"
// #include "protocols/shell/util/argsHelpers.hpp"
// #include "db/query/rbac/Permission.hpp"
// #include "identities/User.hpp"
// #include "rbac/role/Admin.hpp"
// #include "rbac/role/Vault.hpp"
// #include "runtime/Deps.hpp"
// #include "usage/include/UsageManager.hpp"
// #include "CommandUsage.hpp"
//
// using namespace vh;
// using namespace vh::protocols::shell;
// using namespace vh::rbac::model;
//
// // TODO: handle multiple role types and perms
//
// static void parseFlag(const CommandCall& call, const std::string& flag, uint16_t& permissions, const unsigned int bitPosition) {
//     if (hasFlag(call, flag) || hasFlag(call, "allow-" + flag)) permissions |= (1 << bitPosition);
//     else if (hasFlag(call, "deny-" + flag)) permissions &= ~(1 << bitPosition);
// }
//
// static uint16_t parseUserRolePermissions(const CommandCall& call, uint16_t permissions = 0) {
//     parseFlag(call, "manage-encryption-keys", permissions, 0);
//     parseFlag(call, "manage-admins", permissions, 1);
//     parseFlag(call, "manage-users", permissions, 2);
//     parseFlag(call, "manage-groups", permissions, 3);
//     parseFlag(call, "manage-roles", permissions, 4);
//     parseFlag(call, "manage-settings", permissions, 5);
//     parseFlag(call, "manage-vaults", permissions, 6);
//     parseFlag(call, "manage-api-keys", permissions, 7);
//     parseFlag(call, "audit-log-access", permissions, 8);
//     parseFlag(call, "create-vaults", permissions, 9);
//
//     return permissions;
// }
//
// static uint16_t parseVaultRolePermissions(const CommandCall& call, uint16_t permissions = 0) {
//     parseFlag(call, "manage-vault", permissions, 0);
//     parseFlag(call, "manage-access", permissions, 1);
//     parseFlag(call, "manage-tags", permissions, 2);
//     parseFlag(call, "manage-metadata", permissions, 3);
//     parseFlag(call, "manage-versions", permissions, 4);
//     parseFlag(call, "manage-file-locks", permissions, 5);
//     parseFlag(call, "share", permissions, 6);
//     parseFlag(call, "sync", permissions, 7);
//     parseFlag(call, "create", permissions, 8);
//     parseFlag(call, "download", permissions, 9);
//     parseFlag(call, "delete", permissions, 10);
//     parseFlag(call, "rename", permissions, 11);
//     parseFlag(call, "move", permissions, 12);
//     parseFlag(call, "list", permissions, 13);
//
//     return permissions;
// }
//
// static std::shared_ptr<Role> resolveRole(const std::string& arg) {
//     if (const auto roleIdOpt = parseUInt(arg)) {
//         const auto role = db::query::rbac::Permission::getRole(*roleIdOpt);
//         if (!role) throw std::runtime_error("role with ID " + std::to_string(*roleIdOpt) + " not found");
//         return role;
//     }
//
//     const auto role = db::query::rbac::Permission::getRoleByName(std::string(arg));
//     if (!role) throw std::runtime_error("role with name '" + std::string(arg) + "' not found");
//     return role;
// }
//
// static CommandResult handleRolesList(const CommandCall& call) {
//     if (!call.user->canManageRoles()) return invalid("roles list: can't manage");
//
//     const auto usage = resolveUsage({"role", "list"});
//     validatePositionals(call, usage);
//
//     auto params = parseListQuery(call);
//     std::vector<std::shared_ptr<Role>> roles;
//
//     if (hasFlag(call, "user")) roles = db::query::rbac::Permission::listUserRoles(std::move(params));
//     else if (hasFlag(call, "vault")) roles = db::query::rbac::Permission::listVaultRoles(std::move(params));
//     else roles = db::query::rbac::Permission::listRoles(std::move(params));
//
//     return ok(to_string(roles));
// }
//
// static CommandResult handleRoleInfo(const CommandCall& call) {
//     const auto usage = resolveUsage({"role", "info"});
//     validatePositionals(call, usage);
//     const auto role = resolveRole(call.positionals[0]);
//     return ok(to_string(role));
// }
//
// static std::shared_ptr<Role> resolveFromRoleSameType(const std::string& from,
//                                                      const std::string_view expected_type) {
//     const auto r = resolveRole(from);
//     if (r->type != expected_type)
//         throw std::runtime_error("mismatched --from type: expected '" +
//                                  std::string(expected_type) + "', got '" + r->type + "'");
//     return r;
// }
//
// static void assignDescIfAvailable(const CommandCall& call, const std::shared_ptr<CommandUsage>& usage,
//                                   const std::shared_ptr<Role>& role) {
//     const auto descAliases = usage->resolveOptional("description")->option_tokens;
//     if (const auto descOpt = optVal(call, descAliases))
//         role->description = *descOpt;
// }
//
// static void assignPermissions(const CommandCall& call, const std::shared_ptr<Role>& role) {
//     if (!role) throw std::runtime_error("assignPermissions: role is null");
//     if (role->type != "user" && role->type != "vault")
//         throw std::runtime_error("assignPermissions: unknown role type: " + role->type);
//
//     role->permissions = role->type == "admin"
//         ? parseUserRolePermissions(call, role->permissions)
//         : parseVaultRolePermissions(call, role->permissions);
// }
//
// static CommandResult handleRoleCreate(const CommandCall& call) {
//     const auto usage = resolveUsage({"role", "create"});
//     validatePositionals(call, usage);
//
//     const auto type = call.positionals[0];
//     const auto name = call.positionals[1];
//
//     if (type != "admin" && type != "vault")
//         return invalid("roles create: type must be either 'admin' or 'vault'");
//
//     const auto role = std::make_shared<Role>();
//     role->name = name,
//     role->type = type;
//     assignDescIfAvailable(call, usage, role);
//
//     const auto fromAliases = usage->resolveOptional("from")->option_tokens;
//     if (const auto fromOpt = optVal(call, fromAliases)) {
//         const auto from = resolveFromRoleSameType(*fromOpt, type);
//         role->permissions = from->permissions;
//     }
//
//     assignPermissions(call, role);
//
//     role->id = db::query::rbac::Permission::addRole(role);
//
//     return ok("Role created successfully: " + to_string(role));
// }
//
// static CommandResult handleRoleUpdate(const CommandCall& call) {
//     constexpr const auto* ERR = "roles update";
//
//     if (!call.user->canManageRoles()) return invalid("roles update: can't manage");
//
//     const auto usage = resolveUsage({"role", "update"});
//     validatePositionals(call, usage);
//
//     const auto rLkp = resolveRole(call.positionals[0], ERR);
//     if (!rLkp || !rLkp.ptr) return invalid(rLkp.error);
//     const auto role = rLkp.ptr;
//
//     if (role->type == "user" && !call.user->canManageUsers())
//         return invalid("roles update: you do not have permission to update user roles");
//
//     if (role->type == "vault" && !call.user->canManageVaults())
//         return invalid("roles update: you do not have permission to update vault roles");
//
//     if (const auto newNameOpt = optVal(call, usage->resolveOptional("name")->option_tokens)) {
//         if (newNameOpt->empty()) return invalid("roles update: --name cannot be empty");
//         role->name = *newNameOpt;
//     }
//
//     assignDescIfAvailable(call, usage, role);
//     assignPermissions(call, role);
//
//     db::query::rbac::Permission::updateRole(role);
//
//     return ok("Role updated successfully:\n" + to_string(role));
// }
//
// static CommandResult handleRoleDelete(const CommandCall& call) {
//     constexpr const auto* ERR = "roles delete";
//
//     const auto usage = resolveUsage({"role", "delete"});
//     validatePositionals(call, usage);
//
//     const auto rLkp = resolveRole(call.positionals[0], ERR);
//     if (!rLkp || !rLkp.ptr) return invalid(rLkp.error);
//     const auto role = rLkp.ptr;
//
//     db::query::rbac::Permission::deleteRole(role->id);
//     return ok("Role deleted successfully: " + to_string(role));
// }
//
// static bool isRoleMatch(const std::string& cmd, const std::string_view input) {
//     return isCommandMatch({"role", cmd}, input);
// }
//
// static CommandResult handle_role(const CommandCall& call) {
//     const auto usageManager = runtime::Deps::get().shellUsageManager;
//     if (call.positionals.empty()) return usage(call.constructFullArgs());
//
//     const auto [sub, subcall] = descend(call);
//
//     if (isRoleMatch("create", sub)) return handleRoleCreate(subcall);
//     if (isRoleMatch("update", sub)) return handleRoleUpdate(subcall);
//     if (isRoleMatch("delete", sub)) return handleRoleDelete(subcall);
//     if (isRoleMatch("info", sub)) return handleRoleInfo(subcall);
//     if (isRoleMatch("list", sub)) return handleRolesList(subcall);
//
//     return invalid(call.constructFullArgs(), "Unknown roles subcommand: '" + std::string(sub) + "'");
// }
//
// void commands::registerRoleCommands(const std::shared_ptr<Router>& r) {
//     const auto usageManager = runtime::Deps::get().shellUsageManager;
//     r->registerCommand(usageManager->resolve("role"), handle_role);
// }
