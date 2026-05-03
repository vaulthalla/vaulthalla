#include "share/Scope.hpp"

#include <sstream>
#include <stdexcept>
#include <vector>

namespace vh::share {
namespace {

std::vector<std::string> splitComponents(const std::string& path) {
    std::vector<std::string> components;
    std::stringstream stream(path);
    std::string component;

    while (std::getline(stream, component, '/')) {
        if (component.empty() || component == ".") continue;

        if (component == "..") {
            if (!components.empty() && components.back() != "..") components.pop_back();
            else components.emplace_back("..");
            continue;
        }

        components.push_back(component);
    }

    return components;
}

bool hasEscapeComponent(const std::string& path) {
    const auto components = splitComponents(path);
    for (const auto& component : components)
        if (component == "..") return true;
    return false;
}

bool isMkdirTargetAllowed(const Principal& principal, const ScopeRequest& request) {
    if (request.operation != Operation::Mkdir) return true;
    if (request.target_type) return *request.target_type == TargetType::Directory;
    return principal.grant.target_type == TargetType::Directory;
}

}

std::string Scope::normalizeVaultPath(const std::string_view path) {
    if (path.find('\0') != std::string_view::npos)
        throw std::invalid_argument("share scope path contains invalid byte");

    std::string source{path};
    if (source.empty()) source = "/";
    if (source.front() != '/') source.insert(source.begin(), '/');

    const auto components = splitComponents(source);
    if (components.empty()) return "/";

    std::string out;
    for (const auto& component : components) {
        out.push_back('/');
        out += component;
    }
    return out;
}

bool Scope::contains(const std::string_view rootPath, const std::string_view candidatePath) {
    const auto root = normalizeVaultPath(rootPath);
    const auto candidate = normalizeVaultPath(candidatePath);

    if (hasEscapeComponent(root) || hasEscapeComponent(candidate)) return false;
    if (root == "/") return true;
    if (candidate == root) return true;
    if (!candidate.starts_with(root)) return false;
    return candidate.size() > root.size() && candidate[root.size()] == '/';
}

ScopeDecision Scope::authorize(const Principal& principal, const ScopeRequest& request) {
    ScopeDecision decision;
    decision.normalized_path = normalizeVaultPath(request.path);

    if (!principal.hasVault(request.vault_id)) {
        decision.reason = "vault_mismatch";
        return decision;
    }

    if (hasEscapeComponent(decision.normalized_path)) {
        decision.reason = "path_escape";
        return decision;
    }

    if (!contains(principal.root_path, decision.normalized_path)) {
        decision.reason = "outside_scope";
        return decision;
    }

    if (!principal.allows(request.operation)) {
        decision.reason = "operation_not_granted";
        return decision;
    }

    if (!isMkdirTargetAllowed(principal, request)) {
        decision.reason = "invalid_mkdir_target";
        return decision;
    }

    decision.allowed = true;
    decision.reason = "allowed";
    return decision;
}

}
