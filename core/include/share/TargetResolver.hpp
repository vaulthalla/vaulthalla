#pragma once

#include "share/Principal.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vh::fs::model { struct Entry; }
namespace vh::rbac { class Actor; }

namespace vh::share {

enum class TargetPathMode { VaultRelative, ShareRelative };

struct TargetResolveRequest {
    std::string path{"/"};
    Operation operation{Operation::Metadata};
    TargetPathMode path_mode{TargetPathMode::VaultRelative};
    std::optional<TargetType> expected_target_type{};
    std::optional<uint32_t> vault_id{};
};

struct ResolvedTarget {
    uint32_t vault_id{};
    uint32_t root_entry_id{};
    std::string root_path{"/"};
    std::string requested_path{"/"};
    std::string vault_path{"/"};
    std::string share_path{"/"};
    Operation operation{Operation::Metadata};
    TargetType target_type{TargetType::Directory};
    std::shared_ptr<fs::model::Entry> root_entry;
    std::shared_ptr<fs::model::Entry> entry;
};

class TargetEntryProvider {
public:
    virtual ~TargetEntryProvider() = default;

    [[nodiscard]] virtual std::shared_ptr<fs::model::Entry> getEntryById(uint32_t entryId) = 0;
    [[nodiscard]] virtual std::shared_ptr<fs::model::Entry> getEntryByVaultPath(
        uint32_t vaultId,
        std::string_view vaultPath
    ) = 0;
    [[nodiscard]] virtual std::vector<std::shared_ptr<fs::model::Entry>> listChildren(uint32_t parentEntryId) = 0;
};

class TargetResolver {
public:
    TargetResolver();
    explicit TargetResolver(std::shared_ptr<TargetEntryProvider> provider);

    [[nodiscard]] ResolvedTarget resolve(const Principal& principal, TargetResolveRequest request) const;
    [[nodiscard]] ResolvedTarget resolve(const rbac::Actor& actor, TargetResolveRequest request) const;
    [[nodiscard]] std::vector<std::shared_ptr<fs::model::Entry>> listChildren(
        const Principal& principal,
        const ResolvedTarget& target
    ) const;
    [[nodiscard]] std::vector<std::shared_ptr<fs::model::Entry>> listChildren(
        const rbac::Actor& actor,
        const ResolvedTarget& target
    ) const;

    [[nodiscard]] static std::string normalizeRequestedPath(std::string_view path);
    [[nodiscard]] static std::string toVaultPath(
        const Principal& principal,
        std::string_view requestedPath,
        TargetPathMode mode
    );
    [[nodiscard]] static std::string shareRelativePath(const Principal& principal, std::string_view vaultPath);
    [[nodiscard]] static TargetType targetTypeOf(const fs::model::Entry& entry);

private:
    std::shared_ptr<TargetEntryProvider> provider_;
};

}
