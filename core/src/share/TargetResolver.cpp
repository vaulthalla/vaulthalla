#include "share/TargetResolver.hpp"

#include "db/query/fs/Directory.hpp"
#include "db/query/fs/Entry.hpp"
#include "db/query/fs/File.hpp"
#include "fs/model/Directory.hpp"
#include "fs/model/Entry.hpp"
#include "fs/model/File.hpp"
#include "rbac/Actor.hpp"
#include "rbac/fs/policy/Share.hpp"
#include "share/Scope.hpp"

#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace vh::share {
namespace {
class QueryTargetEntryProvider final : public TargetEntryProvider {
public:
    std::shared_ptr<fs::model::Entry> getEntryById(const uint32_t entryId) override {
        return db::query::fs::Entry::getFSEntryById(entryId);
    }

    std::shared_ptr<fs::model::Entry> getEntryByVaultPath(
        const uint32_t vaultId,
        const std::string_view vaultPath
    ) override {
        const auto path = std::filesystem::path{std::string(vaultPath)};
        if (auto file = db::query::fs::File::getFileByPath(vaultId, path)) return file;
        if (!db::query::fs::Directory::directoryExists(vaultId, path)) return nullptr;
        return db::query::fs::Directory::getDirectoryByPath(vaultId, path);
    }

    std::vector<std::shared_ptr<fs::model::Entry>> listChildren(const uint32_t parentEntryId) override {
        return db::query::fs::Entry::listDir(parentEntryId, false);
    }
};

std::vector<std::string> splitStrict(const std::string_view path) {
    if (path.empty()) throw std::invalid_argument("Share filesystem path is required");
    if (path.find('\0') != std::string_view::npos)
        throw std::invalid_argument("Share filesystem path contains invalid byte");

    std::vector<std::string> components;
    std::stringstream stream{std::string(path)};
    std::string component;
    while (std::getline(stream, component, '/')) {
        if (component.empty() || component == ".") continue;
        if (component == "..") throw std::runtime_error("Share filesystem path escapes share root");
        components.push_back(component);
    }
    return components;
}

std::string assembleAbsolutePath(const std::vector<std::string>& components) {
    if (components.empty()) return "/";

    std::string out;
    for (const auto& component : components) {
        out.push_back('/');
        out += component;
    }
    return out;
}

std::string joinRootAndSharePath(const std::string_view rootPath, const std::string_view sharePath) {
    const auto root = Scope::normalizeVaultPath(rootPath);
    const auto share = TargetResolver::normalizeRequestedPath(sharePath);
    if (share == "/") return root;
    if (root == "/") return share;
    return root + share;
}

bool entryHasVault(const fs::model::Entry& entry, const uint32_t vaultId) {
    return entry.vault_id && *entry.vault_id == static_cast<int32_t>(vaultId);
}

std::string entryPath(const fs::model::Entry& entry) {
    return Scope::normalizeVaultPath(entry.path.string());
}

void requireEntryType(const fs::model::Entry& entry, const TargetType expected, const std::string_view reason) {
    const auto actual = TargetResolver::targetTypeOf(entry);
    if (actual != expected) throw std::runtime_error(std::string(reason));
}

void requireValidPrincipal(const Principal& principal) {
    if (principal.share_id.empty() || principal.share_session_id.empty())
        throw std::runtime_error("Share principal is not resolved");
    if (principal.vault_id == 0) throw std::runtime_error("Share principal has no vault");
    if (principal.root_entry_id == 0) throw std::runtime_error("Share principal has no root entry");
}
}

TargetResolver::TargetResolver() : TargetResolver(std::make_shared<QueryTargetEntryProvider>()) {}

TargetResolver::TargetResolver(std::shared_ptr<TargetEntryProvider> provider) : provider_(std::move(provider)) {
    if (!provider_) throw std::invalid_argument("Share target resolver provider is required");
}

std::string TargetResolver::normalizeRequestedPath(const std::string_view path) {
    return assembleAbsolutePath(splitStrict(path));
}

std::string TargetResolver::toVaultPath(
    const Principal& principal,
    const std::string_view requestedPath,
    const TargetPathMode mode
) {
    requireValidPrincipal(principal);
    if (mode == TargetPathMode::ShareRelative)
        return joinRootAndSharePath(principal.root_path, requestedPath);
    return normalizeRequestedPath(requestedPath);
}

std::string TargetResolver::shareRelativePath(const Principal& principal, const std::string_view vaultPath) {
    const auto root = Scope::normalizeVaultPath(principal.root_path);
    const auto candidate = normalizeRequestedPath(vaultPath);
    if (!Scope::contains(root, candidate)) throw std::runtime_error("Share filesystem target escapes share root");
    if (candidate == root) return "/";
    if (root == "/") return candidate;
    return candidate.substr(root.size());
}

TargetType TargetResolver::targetTypeOf(const fs::model::Entry& entry) {
    return entry.isDirectory() ? TargetType::Directory : TargetType::File;
}

ResolvedTarget TargetResolver::resolve(const Principal& principal, TargetResolveRequest request) const {
    requireValidPrincipal(principal);

    const auto vaultId = request.vault_id.value_or(principal.vault_id);
    const auto vaultPath = toVaultPath(principal, request.path, request.path_mode);

    const auto scopeDecision = Scope::authorize(principal, {
        .vault_id = vaultId,
        .path = vaultPath,
        .operation = request.operation,
        .target_type = request.expected_target_type
    });
    if (!scopeDecision.allowed)
        throw std::runtime_error("Share filesystem scope denied: " + scopeDecision.reason);

    if (principal.grant.target_type == TargetType::File &&
        scopeDecision.normalized_path != Scope::normalizeVaultPath(principal.root_path)) {
        throw std::runtime_error("Share file target does not allow descendants");
    }

    auto rootEntry = provider_->getEntryById(principal.root_entry_id);
    if (!rootEntry) throw std::runtime_error("Share root entry not found");
    if (!entryHasVault(*rootEntry, principal.vault_id))
        throw std::runtime_error("Share root entry vault mismatch");
    if (entryPath(*rootEntry) != Scope::normalizeVaultPath(principal.root_path))
        throw std::runtime_error("Share root entry has moved");
    requireEntryType(*rootEntry, principal.grant.target_type, "Share root entry type mismatch");

    auto entry = provider_->getEntryByVaultPath(vaultId, scopeDecision.normalized_path);
    if (!entry) throw std::runtime_error("Share filesystem target not found");
    if (!entryHasVault(*entry, vaultId)) throw std::runtime_error("Share filesystem target vault mismatch");
    if (entryPath(*entry) != scopeDecision.normalized_path)
        throw std::runtime_error("Share filesystem target path mismatch");
    if (!Scope::contains(principal.root_path, entryPath(*entry)))
        throw std::runtime_error("Share filesystem target escapes share root");

    const auto targetType = targetTypeOf(*entry);
    if (request.expected_target_type && targetType != *request.expected_target_type)
        throw std::runtime_error("Share filesystem target type mismatch");
    if (request.operation == Operation::List && targetType != TargetType::Directory)
        throw std::runtime_error("Share filesystem list target is not a directory");

    ResolvedTarget resolved{
        .vault_id = vaultId,
        .root_entry_id = principal.root_entry_id,
        .root_path = Scope::normalizeVaultPath(principal.root_path),
        .requested_path = normalizeRequestedPath(request.path),
        .vault_path = scopeDecision.normalized_path,
        .share_path = shareRelativePath(principal, scopeDecision.normalized_path),
        .operation = request.operation,
        .target_type = targetType,
        .root_entry = std::move(rootEntry),
        .entry = std::move(entry)
    };

    const auto rbacDecision = rbac::fs::policy::Share::evaluate(principal, resolved);
    if (!rbacDecision.allowed)
        throw std::runtime_error(
            "Share filesystem RBAC denied: " + rbac::fs::policy::reasonToString(rbacDecision.reason));

    return resolved;
}

ResolvedTarget TargetResolver::resolve(const rbac::Actor& actor, TargetResolveRequest request) const {
    if (!actor.isShare() || !actor.sharePrincipal())
        throw std::runtime_error("Share filesystem actor is required");
    return resolve(*actor.sharePrincipal(), std::move(request));
}

std::vector<std::shared_ptr<fs::model::Entry>> TargetResolver::listChildren(
    const Principal& principal,
    const ResolvedTarget& target
) const {
    if (!target.entry) throw std::runtime_error("Share filesystem list target is missing");
    if (target.target_type != TargetType::Directory)
        throw std::runtime_error("Share filesystem list target is not a directory");

    auto entries = provider_->listChildren(target.entry->id);
    for (const auto& entry : entries) {
        if (!entry) throw std::runtime_error("Share filesystem list contains missing entry");
        if (!entryHasVault(*entry, target.vault_id))
            throw std::runtime_error("Share filesystem list contains cross-vault entry");
        if (!Scope::contains(principal.root_path, entryPath(*entry)))
            throw std::runtime_error("Share filesystem list contains entry outside share root");
    }
    return entries;
}

std::vector<std::shared_ptr<fs::model::Entry>> TargetResolver::listChildren(
    const rbac::Actor& actor,
    const ResolvedTarget& target
) const {
    if (!actor.isShare() || !actor.sharePrincipal())
        throw std::runtime_error("Share filesystem actor is required");
    return listChildren(*actor.sharePrincipal(), target);
}

}
