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
    using resolver::Target;
    using resolver::Request;
    using resolver::Resolved;
    using resolver::Status;

    Resolved Resolver::resolve(const Request &req) {
        Resolved res;

        if (!req.fuseReq) {
            res.setStatus(Status::MissingFuseContext, EINVAL);
            return res;
        }

        constexpr std::array functions{
            resolveIdentity,
            resolveEntry,
            resolvePath,
            resolveEntryForPath,
            resolveEngine,
            enforcePermissions
        };

        for (const auto &func: functions) {
            if (!func(req, res)) {
                if (res.ok()) res.setStatus(Status::InternalError, EIO);
                break;
            }

            if (!res.ok()) break;
        }

        if (!res.ino && (res.entry || res.path)) {
            log::Registry::fuse()->debug("[{}] Attempting to resolve inode from entry or path: entry: {}, path: {}",
                                         req.caller, res.entry ? res.entry->name : "null",
                                         res.path && !res.path->empty() ? res.path->string() : "null");
            if (res.path) res.ino = runtime::Deps::get().fsCache->getOrAssignInode(*res.path);
            if (!res.ino && res.entry && res.entry->inode) res.ino = res.entry->inode;
            if (!res.ino) res.setStatus(Status::MissingIno, EINVAL);
        }

        return res;
    }

    bool Resolver::resolveIdentity(const resolver::Request &req, resolver::Resolved &out) {
        const fuse_ctx *fctx = fuse_req_ctx(req.fuseReq);
        if (!fctx) {
            out.setStatus(Status::MissingFuseContext, EINVAL);
            return false;
        }

        const uid_t uid = fctx->uid;
        const gid_t gid = fctx->gid;

        out.user = db::query::identities::User::getUserByLinuxUID(uid);
        if (!out.user) {
            log::Registry::fuse()->error("[{}] No user found for UID {}", req.caller, uid);
            out.setStatus(Status::MissingUser, EACCES);
            return false;
        }

        out.group = db::query::identities::Group::getGroupByLinuxGID(gid);

        return true;
    }

    bool Resolver::resolveEntry(const resolver::Request &req, resolver::Resolved &out) {
        if (resolver::hasFlag(req.target, Target::Entry)) {
            if (!req.ino) {
                log::Registry::fuse()->debug("[{}] Request target includes Ino but no inode provided", req.caller);
                out.setStatus(Status::MissingIno, EINVAL);
                return false;
            }

            out.entry = runtime::Deps::get().fsCache->getEntry(*req.ino);
            if (!out.entry) {
                log::Registry::fuse()->debug("[{}] Failed to resolve entry", req.caller);
                out.setStatus(Status::MissingEntry, ENOENT);
                return false;
            }
        }

        return true;
    }

    bool Resolver::resolvePath(const resolver::Request &req, resolver::Resolved &out) {
        if (resolver::hasFlag(req.target, Target::Path) || resolver::hasFlag(req.target, Target::EntryForPath)) {
            if (!req.parentIno) {
                log::Registry::fuse()->debug("[{}] Request target includes Path but no parent inode provided",
                                             req.caller);
                out.setStatus(Status::MissingParentIno, EINVAL);
                return false;
            }

            if (!req.childName) {
                log::Registry::fuse()->debug("[{}] Request target includes Child but no child name provided",
                                             req.caller);
                out.setStatus(Status::MissingChildName, EINVAL);
                return false;
            }

            try {
                out.path = runtime::Deps::get().fsCache->resolvePath(*req.parentIno) / *req.childName;
            } catch (const std::exception &e) {
                log::Registry::fuse()->error("[{}] Failed to resolve path: {}", req.caller, e.what());
                out.setStatus(Status::MissingPath, ENOENT);
                return false;
            }
        }

        return true;
    }

    bool Resolver::resolveEntryForPath(const resolver::Request &req, resolver::Resolved &out) {
        log::Registry::fuse()->debug("[{}] Resolving entry for path: {}, parent inode: {}, child name: {}",
                                     req.caller,
                                     out.path ? out.path->string() : "null",
                                     req.parentIno ? std::to_string(*req.parentIno) : "null",
                                     req.childName ? *req.childName : "null");

        if (!resolver::hasFlag(req.target, Target::EntryForPath))
            return true;

        if (!out.path) {
            log::Registry::fuse()->debug("[{}] Request target includes EntryForPath but no path resolved",
                                         req.caller);
            out.setStatus(Status::MissingPath, EINVAL);
            return false;
        }

        out.entry = runtime::Deps::get().fsCache->getEntry(*out.path);
        if (!out.entry) {
            log::Registry::fuse()->error("[{}] Failed to resolve entry for path {}", req.caller,
                                         out.path->string());
            out.setStatus(Status::MissingEntry, ENOENT);
            return false;
        }

        return true;
    }

    static bool enforcePermission(const std::shared_ptr<vh::identities::User> &user,
                                  const rbac::permission::vault::FilesystemAction &action,
                                  const std::shared_ptr<fs::model::Entry> &entry,
                                  const std::optional<std::filesystem::path> &path = std::nullopt) {
        if (!rbac::resolver::Vault::has<rbac::permission::vault::FilesystemAction>({
            .user = user,
            .permission = action,
            .path = path,
            .entry = entry
        })) {
            if (entry)
                log::Registry::fuse()->warn("[create] Access denied for user {} on path {}", user->name,
                                            entry->path.string());
            else if (path)
                log::Registry::fuse()->warn("[create] Access denied for user {} on path {}", user->name,
                                            path->string());
            return false;
        }

        return true;
    }

    bool Resolver::enforcePermissions(const resolver::Request &req, resolver::Resolved &out) {
        const bool checkEntry =
                resolver::hasFlag(req.target, Target::Entry) ||
                resolver::hasFlag(req.target, Target::EntryForPath);

        const bool checkPath =
                resolver::hasFlag(req.target, Target::Path);

        if (req.action) {
            if (checkEntry && !enforcePermission(out.user, *req.action, out.entry)) {
                out.setStatus(Status::AccessDenied, EACCES);
                return false;
            }

            if (checkPath && !enforcePermission(out.user, *req.action, nullptr, out.vaultPath)) {
                out.setStatus(Status::AccessDenied, EACCES);
                return false;
            }
        }

        if (!req.actions.empty()) {
            for (const auto &action: req.actions) {
                if (checkEntry && !enforcePermission(out.user, action, out.entry)) {
                    out.setStatus(Status::AccessDenied, EACCES);
                    return false;
                }

                if (checkPath && !enforcePermission(out.user, action, nullptr, out.vaultPath)) {
                    out.setStatus(Status::AccessDenied, EACCES);
                    return false;
                }
            }
        }

        return true;
    }

    bool Resolver::resolveEngine(const resolver::Request &req, resolver::Resolved &out) {
        (void) req;

        if (req.action || !req.actions.empty() || resolver::hasFlag(req.target, Target::Path) || resolver::hasFlag(
                req.target, Target::EntryForPath)) {
            if (out.entry && out.entry->vault_id)
                out.engine = runtime::Deps::get().storageManager->getEngine(*out.entry->vault_id);

            if (!out.engine && out.path)
                out.engine = runtime::Deps::get().storageManager->resolveStorageEngine(*out.path);

            if (out.engine && out.path) out.vaultPath = out.engine->paths->absRelToAbsRel(
                                            *out.path, fs::model::PathType::FUSE_ROOT, fs::model::PathType::FUSE_ROOT);
            if (!out.engine) {
                out.setStatus(Status::MissingEngine, EIO);
                return false;
            }
            return true;
        }

        return true;
    }
}
