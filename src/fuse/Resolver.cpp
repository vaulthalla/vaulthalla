#include "fuse/Resolver.hpp"
#include "db/query/identities/User.hpp"
#include "db/query/identities/Group.hpp"
#include "runtime/Deps.hpp"
#include "log/Registry.hpp"
#include "fs/cache/Registry.hpp"
#include "fs/model/Entry.hpp"
#include "storage/Manager.hpp"
#include "rbac/resolver/vault/all.hpp"
#include "fs/model/Path.hpp"

namespace vh::fuse {
    using resolver::Request;
    using resolver::Resolved;
    using resolver::Status;
    using resolver::Target;

    namespace {
        [[nodiscard]]
        bool needsEntry(const Request& req) {
            return resolver::hasFlag(req.target, Target::Entry) ||
                   resolver::hasFlag(req.target, Target::EntryForPath);
        }

        [[nodiscard]]
        bool needsPath(const Request& req) {
            return resolver::hasFlag(req.target, Target::Path) ||
                   resolver::hasFlag(req.target, Target::EntryForPath);
        }

        [[nodiscard]]
        std::string_view entryName(const std::shared_ptr<fs::model::Entry>& entry) {
            return entry ? std::string_view(entry->name) : std::string_view("null");
        }
    }

    Resolved Resolver::resolve(const Request& req) {
        Resolved res;

        if (!req.fuseReq) {
            res.setStatus(Status::MissingFuseContext, EINVAL);
            return res;
        }

        if (!resolveIdentity(req, res)) return res;
        if (!resolveEntry(req, res)) return res;
        if (!resolvePath(req, res)) return res;
        if (!resolveEntryForPath(req, res)) return res;
        if (!resolveEngine(req, res)) return res;
        if (!enforcePermissions(req, res)) return res;

        if (!res.ino && (res.entry || res.path)) {
            log::Registry::fuse()->debug(
                "[{}] Attempting to resolve inode from entry or path: entry: {}, path: {}",
                req.caller,
                std::string(entryName(res.entry)),
                res.path && !res.path->empty() ? res.path->string() : "null"
            );

            if (res.path)
                res.ino = runtime::Deps::get().fsCache->getOrAssignInode(*res.path);

            if (!res.ino && res.entry && res.entry->inode)
                res.ino = res.entry->inode;

            if (!res.ino)
                res.setStatus(Status::MissingIno, EINVAL);
        }

        return res;
    }

    bool Resolver::resolveIdentity(const Request& req, Resolved& out) {
        const fuse_ctx* fctx = fuse_req_ctx(req.fuseReq);
        if (!fctx) {
            out.setStatus(Status::MissingFuseContext, EINVAL);
            return false;
        }

        const uid_t uid = fctx->uid;
        const gid_t gid = fctx->gid;

        out.user = db::query::identities::User::getUserByLinuxUID(uid);
        if (!out.user) {
            log::Registry::fuse()->debug("[{}] No user found for UID {}", req.caller, uid);
            out.setStatus(Status::MissingUser, EACCES);
            return false;
        }

        out.group = db::query::identities::Group::getGroupByLinuxGID(gid);
        return true;
    }

    bool Resolver::resolveEntry(const Request& req, Resolved& out) {
        if (!needsEntry(req)) return true;
        if (out.entry) return true;

        if (req.ino) {
            out.entry = runtime::Deps::get().fsCache->getEntry(*req.ino);
            if (out.entry) {
                log::Registry::fuse()->debug(
                    "[{}] Resolved entry from inode {}: {}",
                    req.caller, *req.ino, out.entry->path.string()
                );
                return true;
            }

            log::Registry::fuse()->debug("[{}] Failed to resolve entry from inode {}", req.caller, *req.ino);
        }

        return true;
    }

    bool Resolver::resolvePath(const Request& req, Resolved& out) {
        if (!needsPath(req)) return true;
        if (out.path) return true;

        if (req.parentIno && req.childName) {
            try {
                const auto parentPath = runtime::Deps::get().fsCache->resolvePath(*req.parentIno);
                const auto child = std::filesystem::path(*req.childName).filename();
                out.path = parentPath / child;

                log::Registry::fuse()->debug(
                    "[{}] Resolved path from parent/child: {}",
                    req.caller, out.path->string()
                );
                return true;
            } catch (const std::exception& e) {
                log::Registry::fuse()->debug(
                    "[{}] Failed to resolve path from parent/child: {}",
                    req.caller, e.what()
                );
            }
        }

        if (out.entry) {
            out.path = out.entry->path;
            log::Registry::fuse()->debug(
                "[{}] Resolved path from entry: {}",
                req.caller, out.path->string()
            );
            return true;
        }

        if (req.ino) {
            if (const auto entry = runtime::Deps::get().fsCache->getEntry(*req.ino)) {
                out.entry = entry;
                out.path = entry->path;

                log::Registry::fuse()->debug(
                    "[{}] Resolved path from inode {} via entry: {}",
                    req.caller, *req.ino, out.path->string()
                );
                return true;
            }
        }

        log::Registry::fuse()->debug(
            "[{}] Failed to resolve path: no parent/child path and no inode/entry fallback",
            req.caller
        );
        out.setStatus(Status::MissingPath, ENOENT);
        return false;
    }

    bool Resolver::resolveEntryForPath(const Request& req, Resolved& out) {
        if (!needsEntry(req)) return true;
        if (out.entry) return true;

        log::Registry::fuse()->debug(
            "[{}] Resolving entry for path: {}, parent inode: {}, child name: {}",
            req.caller,
            out.path ? out.path->string() : "null",
            req.parentIno ? std::to_string(*req.parentIno) : "null",
            req.childName ? *req.childName : "null"
        );

        if (!out.path) {
            log::Registry::fuse()->debug("[{}] Need entry but no path resolved", req.caller);
            out.setStatus(Status::MissingPath, EINVAL);
            return false;
        }

        out.entry = runtime::Deps::get().fsCache->getEntry(*out.path);
        if (!out.entry) {
            log::Registry::fuse()->debug("[{}] Failed to resolve entry for path {}", req.caller, out.path->string());

            // Important: for create/lookup of not-yet-existing children, missing entry may be acceptable.
            if (req.action == rbac::permission::vault::FilesystemAction::Upload ||
                req.action == rbac::permission::vault::FilesystemAction::Touch) {
                return true;
            }

            out.setStatus(Status::MissingEntry, ENOENT);
            return false;
        }

        return true;
    }

    static bool enforcePermission(
        const std::shared_ptr<vh::identities::User>& user,
        const rbac::permission::vault::FilesystemAction& action,
        const std::shared_ptr<fs::model::Entry>& entry,
        const std::optional<std::filesystem::path>& path = std::nullopt
    ) {
        if (!rbac::resolver::Vault::has<rbac::permission::vault::FilesystemAction>({
            .user = user,
            .permission = action,
            .path = path,
            .entry = entry
        })) {
            if (entry)
                log::Registry::fuse()->warn("[auth] Access denied for user {} on path {}", user->name, entry->path.string());
            else if (path)
                log::Registry::fuse()->warn("[auth] Access denied for user {} on path {}", user->name, path->string());
            else
                log::Registry::fuse()->warn("[auth] Access denied for user {} with no resolved path/entry", user->name);

            return false;
        }

        return true;
    }

    bool Resolver::enforcePermissions(const Request& req, Resolved& out) {
        const bool checkEntry = needsEntry(req);
        const bool checkPath = needsPath(req);

        if (req.action && (checkEntry || checkPath) && !enforcePermission(out.user, *req.action, out.entry, out.path)) {
            out.setStatus(Status::AccessDenied, EACCES);
            return false;
        }

        for (const auto& action : req.actions)
            if ((checkEntry || checkPath) && !enforcePermission(out.user, action, out.entry, out.path)) {
                out.setStatus(Status::AccessDenied, EACCES);
                return false;
            }

        return true;
    }

    bool Resolver::resolveEngine(const Request& req, Resolved& out) {
        if (!(req.action || !req.actions.empty() || needsPath(req))) return true;

        if (out.entry && out.entry->vault_id)
            out.engine = runtime::Deps::get().storageManager->getEngine(*out.entry->vault_id);

        if (!out.engine && out.path)
            out.engine = runtime::Deps::get().storageManager->resolveStorageEngine(*out.path);

        if (!out.engine) {
            out.setStatus(Status::MissingEngine, EIO);
            return false;
        }

        return true;
    }
}
