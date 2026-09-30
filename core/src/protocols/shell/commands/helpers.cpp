#include "protocols/shell/commands/helpers.hpp"
#include "protocols/shell/types.hpp"
#include "runtime/Deps.hpp"
#include "UsageManager.hpp"
#include "CommandUsage.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <sys/stat.h>
#include <unistd.h>

namespace vh::protocols::shell {

std::shared_ptr<CommandUsage> resolveUsage(const std::vector<std::string>& path) {
    const auto usage = runtime::Deps::get().shellUsageManager->resolve(path);
    if (!usage) throw std::runtime_error("No usage found for '" + (path.empty() ? "root" : path[0]) + "'");
    return usage;
}

void validatePositionals(const CommandCall& call, const std::shared_ptr<CommandUsage>& usage) {
    if (call.positionals.size() != usage->positionals.size())
        throw std::runtime_error("Invalid number of positionals for command '" + usage->primary() + "': expected " +
                                 std::to_string(usage->positionals.size()) + ", got " + std::to_string(call.positionals.size()));
}

std::optional<std::string> secretOutputPathError(const std::string& path) {
    if (path.empty()) return std::string("--output requires a file path");
    if (!std::filesystem::path(path).is_absolute())
        return "--output must be an absolute path (the file is written by the vaulthalla daemon, not in your "
               "current directory): " + path;

    struct stat st{};
    if (::lstat(path.c_str(), &st) == 0 && !S_ISREG(st.st_mode))
        return "--output must be a regular file path, not a symlink or special file: " + path;
    return std::nullopt;
}

void writePrivateFile(const std::string& path, const std::string_view content) {
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) throw std::runtime_error("failed to open " + path + ": " + std::strerror(errno));

    // An existing file keeps its old mode through O_TRUNC; tighten it before any secret byte is written.
    if (::fchmod(fd, 0600) != 0) {
        const auto err = errno;
        ::close(fd);
        throw std::runtime_error("failed to restrict permissions on " + path + ": " + std::strerror(err));
    }

    const char* p = content.data();
    std::size_t left = content.size();
    while (left > 0) {
        const auto n = ::write(fd, p, left);
        if (n < 0) {
            if (errno == EINTR) continue;
            const auto err = errno;
            ::close(fd);
            throw std::runtime_error("failed to write " + path + ": " + std::strerror(err));
        }
        p += n;
        left -= static_cast<std::size_t>(n);
    }

    if (::close(fd) != 0) throw std::runtime_error("failed to close " + path + ": " + std::strerror(errno));
}

void restrictToOwner(const std::string& path) {
    if (::chmod(path.c_str(), 0600) != 0)
        throw std::runtime_error("failed to restrict permissions on " + path + ": " + std::strerror(errno));
}

}
