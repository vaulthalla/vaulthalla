#pragma once

#include <filesystem>
#include <optional>
#include <string>

// The built-in web super-admin account ('admin') has no universal default password. A fresh install gives it a
// random one (16 bytes from the CSPRNG as 32 hex characters), stored through the normal password hash, and writes a
// plaintext copy to <state dir>/super_admin_initial_password (mode 0600, the daemon's user) so the operator can sign
// in. The copy is written only when that credential is created: never again because it was deleted, and never on an
// upgrade, reinstall or restart, which find the 'admin' row already there.
//
// Changing the password is recommended, not required: the generated one is strong, and the remaining concern is the
// plaintext copy. `vh setup set-super-admin-password` (or any change of admin's password) records the rotation and
// removes the copy; deleting the copy alone keeps the password. Nothing here restricts a signed-in session.
//
// State: auth_bootstrap_state.super_admin_password_generated (one row). Whether the copy exists is read from disk.
namespace vh::auth::bootstrap {

inline constexpr const char* kSuperAdminName = "admin";
inline constexpr const char* kInitialPasswordFileName = "super_admin_initial_password";

[[nodiscard]] std::filesystem::path initialPasswordFile();
[[nodiscard]] bool initialPasswordFileExists() noexcept;

// 16 random bytes as 32 lowercase hex characters.
[[nodiscard]] std::string generatePassword();

// For creating the 'admin' row: generates the password, writes its plaintext copy (atomically, 0600) and records it
// as generated. Returns the hash to store. If creating the row then fails, the next startup issues a new one.
[[nodiscard]] std::string issueInitialCredential();

// Is admin's current password still the generated one?
[[nodiscard]] bool superAdminPasswordIsGenerated();

// The generated password is still in use and its plaintext copy is still on disk. What the web console (for the
// super admin only) and `vh setup nginx` warn about.
[[nodiscard]] bool initialPasswordExposed();

// After admin's password changed: records the rotation, then removes the plaintext copy. Returns why the copy could
// not be removed, if it couldn't; the password change stands either way.
std::optional<std::string> onSuperAdminPasswordChanged();

// Removes the plaintext copy and keeps the password. Returns why it failed, if it did; a missing file is not an error.
std::optional<std::string> removeInitialPasswordFile();

// Startup, after the protected principals are reconciled. Installs created before 1.8.0 may still use the retired
// universal default password: replace it with a generated one (plaintext copy written as on a fresh install) and end
// admin's refresh tokens. Any other password is left alone. Returns true when it replaced one.
bool retireLegacyDefaultPassword();

}
