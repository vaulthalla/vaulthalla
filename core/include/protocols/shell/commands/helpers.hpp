#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vh::protocols::shell {

class CommandUsage;
struct CommandCall;

std::shared_ptr<CommandUsage> resolveUsage(const std::vector<std::string>& path);
void validatePositionals(const CommandCall& call, const std::shared_ptr<CommandUsage>& usage);

// `--output` files for secret material are written by the daemon, not by the `vh` client, so a relative path
// would land in the daemon's working directory. Returns why `path` is unacceptable (not absolute, or an existing
// symlink/non-regular file), or nullopt when it is fine.
[[nodiscard]] std::optional<std::string> secretOutputPathError(const std::string& path);

// Creates or truncates `path` with mode 0600 (never follows a symlink) and writes `content`. Throws on failure.
void writePrivateFile(const std::string& path, std::string_view content);

// Forces an existing file (e.g. one written by gpg) to mode 0600. Throws on failure.
void restrictToOwner(const std::string& path);

}
