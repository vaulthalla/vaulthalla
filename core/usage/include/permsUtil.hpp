#pragma once

#include <array>
#include <string_view>
#include <vector>
#include <cstddef>

// --- Base sets (all constexpr, string_views refer to string literals = static storage) ---
static constexpr std::array<std::string_view, 9> ADMIN_SHELL_PERMS{{
    "manage-encryption-keys",
    "manage-admins",
    "manage-users",
    "manage-groups",
    "manage-vaults",
    "manage-roles",
    "manage-api-keys",
    "audit-log-access",
    "create-vaults"
}};

static constexpr std::array<std::string_view, 14> VAULT_SHELL_PERMS{{
    "manage-vault",
    "manage-access",
    "manage-tags",
    "manage-metadata",
    "manage-versions",
    "manage-file-locks",
    "share",
    "sync",
    "create",
    "download",
    "delete",
    "rename",
    "move",
    "list"
}};

// --- constexpr concat for std::array ---
template <class T, std::size_t N, std::size_t M>
constexpr std::array<T, N + M> concat(const std::array<T, N>& a, const std::array<T, M>& b) {
    std::array<T, N + M> out{};
    for (std::size_t i = 0; i < N; ++i) out[i] = a[i];
    for (std::size_t j = 0; j < M; ++j) out[N + j] = b[j];
    return out;
}

// --- ALL as a single constexpr array ---
static constexpr auto ALL_SHELL_PERMS = concat(ADMIN_SHELL_PERMS, VAULT_SHELL_PERMS);
// type is: std::array<std::string_view, 23>

// --- If you want a std::vector<std::string_view> at runtime (not constexpr) ---
inline const std::vector<std::string_view> ALL_SHELL_PERMS_VEC{
    ALL_SHELL_PERMS.begin(), ALL_SHELL_PERMS.end()
};

// --- If you specifically need std::vector<std::string> at runtime ---
inline const std::vector<std::string> ALL_SHELL_PERMS_STR = [] {
    std::vector<std::string> v;
    v.reserve(ALL_SHELL_PERMS.size());
    for (auto sv : ALL_SHELL_PERMS) v.emplace_back(sv);
    return v;
}();
