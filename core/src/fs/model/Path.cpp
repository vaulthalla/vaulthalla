#include "fs/model/Path.hpp"
#include "config/Registry.hpp"
#include "log/Registry.hpp"

#include <paths.h>

using namespace vh::fs::model;
using namespace vh::config;

Path::Path(const std::filesystem::path& vaultFuseMount, const std::filesystem::path& vaultBackingMount)
    : fuseRoot(paths::getMountPath()),
      vaultRoot(fuseRoot / stripLeadingSlash(vaultFuseMount)),
      cacheRoot(paths::getBackingPath() /
                stripLeadingSlash(paths::getCachePath()) /
                stripLeadingSlash(vaultBackingMount)),
      thumbnailRoot(cacheRoot / "thumbnails"),
      fileCacheRoot(cacheRoot / "files"),
      backingRoot(paths::getBackingPath()),
      backingVaultRoot(paths::getBackingPath() /
                       stripLeadingSlash(vaultBackingMount)) {

    log::Registry::storage()->debug("[Path] Initialized paths:\nfuseRoot: {}\nvaultRoot: {}\n"
                                  "cacheRoot: {}\nthumbnailRoot: {}\nfileCacheRoot: {}\n"
                                  "backingRoot: {}\nbackingVaultRoot: {}",
                                  fuseRoot.string(), vaultRoot.string(), cacheRoot.string(),
                                  thumbnailRoot.string(), fileCacheRoot.string(),
                                  backingRoot.string(), backingVaultRoot.string());
}

std::filesystem::path Path::absPath(const std::filesystem::path& relPath, const PathType& type) const {
    switch (type) {
    case PathType::FUSE_ROOT:
        if (relPath.string() == "/") return fuseRoot;
        return fuseRoot / stripLeadingSlash(relPath);
    case PathType::VAULT_ROOT:
        if (relPath.string() == "/") return vaultRoot;
        return vaultRoot / stripLeadingSlash(relPath);
    case PathType::CACHE_ROOT:
        if (relPath.string() == "/") return cacheRoot;
        return cacheRoot / stripLeadingSlash(relPath);
    case PathType::THUMBNAIL_ROOT:
        if (relPath.string() == "/") return thumbnailRoot;
        return thumbnailRoot / stripLeadingSlash(relPath);
    case PathType::FILE_CACHE_ROOT:
        if (relPath.string() == "/") return fileCacheRoot;
        return fileCacheRoot / stripLeadingSlash(relPath);
    case PathType::BACKING_ROOT:
        if (relPath.string() == "/") return backingRoot;
        return backingRoot / stripLeadingSlash(relPath);
    case PathType::BACKING_VAULT_ROOT:
        if (relPath.string() == "/") return backingVaultRoot;
        return backingVaultRoot / stripLeadingSlash(relPath);
    default:
        throw std::invalid_argument("Invalid PathType");
    }
}

std::filesystem::path Path::relPath(const std::filesystem::path& absPath, const PathType& type) const {
    switch (type) {
    case PathType::FUSE_ROOT:
        return absPath.lexically_relative(fuseRoot).make_preferred();
    case PathType::VAULT_ROOT:
        return absPath.lexically_relative(vaultRoot).make_preferred();
    case PathType::CACHE_ROOT:
        return absPath.lexically_relative(cacheRoot).make_preferred();
    case PathType::THUMBNAIL_ROOT:
        return absPath.lexically_relative(thumbnailRoot).make_preferred();
    case PathType::FILE_CACHE_ROOT:
        return absPath.lexically_relative(fileCacheRoot).make_preferred();
    case PathType::BACKING_ROOT:
        return absPath.lexically_relative(backingRoot).make_preferred();
    case PathType::BACKING_VAULT_ROOT:
        return absPath.lexically_relative(backingVaultRoot).make_preferred();
    default:
        throw std::invalid_argument("Invalid PathType");
    }
}

std::filesystem::path Path::absRelToRoot(const std::filesystem::path& path, const PathType& type) const {
    const auto rel = relPath(path, type);
    if (rel.empty() || rel.string().starts_with("..")) return makeAbsolute(path.filename());
    return makeAbsolute(rel.lexically_normal());
}

std::filesystem::path Path::absRelToAbsRel(const std::filesystem::path& path, const PathType& initial, const PathType& target) const {
    return absRelToRoot(absPath(path, initial), target);
}
