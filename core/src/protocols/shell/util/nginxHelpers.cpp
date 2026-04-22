#include "protocols/shell/util/nginxHelpers.hpp"

namespace vh::protocols::shell::util {

bool isManagedSiteSymlinkTarget(const std::filesystem::path& linkPath) {
    if (!std::filesystem::is_symlink(linkPath)) return false;
    const auto target = std::filesystem::read_symlink(linkPath);
    return target == std::filesystem::path(kNginxSiteAvailable) || target == std::filesystem::path("../sites-available/vaulthalla");
}

}
