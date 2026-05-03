#pragma once

#include "share/Principal.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace vh::share {

struct ScopeRequest {
    uint32_t vault_id{};
    std::string path{"/"};
    Operation operation{Operation::Metadata};
    std::optional<TargetType> target_type{};
};

struct ScopeDecision {
    bool allowed{false};
    std::string reason{"denied"};
    std::string normalized_path{"/"};
};

class Scope {
public:
    static std::string normalizeVaultPath(std::string_view path);
    static bool contains(std::string_view rootPath, std::string_view candidatePath);
    static ScopeDecision authorize(const Principal& principal, const ScopeRequest& request);
};

}
