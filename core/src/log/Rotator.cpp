#include "log/Rotator.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <system_error>

#ifdef __linux__
  #include <sys/file.h>
  #include <sys/stat.h>
  #include <unistd.h>
#endif

using namespace vh::log;

Rotator::Rotator(Options opts)
    : opts_(std::move(opts)) {
    if (opts_.active_path.empty())
        throw std::invalid_argument("LogRotator: active_path is empty.");

    dir_  = opts_.active_path.parent_path();
    base_ = opts_.active_path.stem().string();      // e.g. "vaulthalla"
    ext_  = opts_.active_path.extension().string(); // e.g. ".log"

    if (!opts_.rotated_filter) {
        rotated_regex_ = std::regex(
            std::string("^")
            + escapeRx(base_) + R"(\.\d{8}-\d{6})" + escapeRx(ext_)
            + R"((?:\.gz|\.zst)?)" + "$"
        );
        opts_.rotated_filter = [this](const std::filesystem::path& p) {
            return std::regex_match(p.filename().string(), rotated_regex_);
        };
    }
    if (!opts_.lock_dir) opts_.lock_dir = dir_;
}

void Rotator::maybeRotate() const {
    std::scoped_lock lk(m_);
    if (const auto reason = rotationReason(); reason != RotateReason::None) rotateImpl(reason);
    pruneImpl();
}

void Rotator::forceRotate() const {
    std::scoped_lock lk(m_);
    rotateImpl(RotateReason::Forced);
    pruneImpl();
}

void Rotator::pruneOnly() const {
    std::scoped_lock lk(m_);
    pruneImpl();
}

// ===== helpers =====

std::string Rotator::escapeRx(const std::string& s) {
    static const std::regex special_chars(R"([.^$|()\[\]{}*+?\\])");
    return std::regex_replace(s, special_chars, R"(\$&)");
}

std::chrono::system_clock::time_point Rotator::to_sys(std::filesystem::file_time_type tp) {
    using namespace std::chrono;
    auto sctp = time_point_cast<system_clock::duration>(tp - decltype(tp)::clock::now() + system_clock::now());
    return sctp;
}

Rotator::RotateReason Rotator::rotationReason() const {
    std::error_code ec;

    if (!std::filesystem::exists(opts_.active_path, ec)) {
        return RotateReason::None; // nothing to rotate
    }

    if (opts_.max_bytes && !ec && std::filesystem::file_size(opts_.active_path, ec) >= *opts_.max_bytes)
        return RotateReason::Size;

    if (opts_.max_interval) {
        const auto mtime = std::filesystem::last_write_time(opts_.active_path, ec);
        if (!ec) {
            using namespace std::chrono;
            const auto now  = system_clock::now();
            const auto then = to_sys(mtime);
            if (now - then >= *opts_.max_interval) return RotateReason::Interval;
        }
    }

    return RotateReason::None;
}

std::string Rotator::nowStamp() {
    using namespace std::chrono;
    auto t = system_clock::to_time_t(system_clock::now());
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::ostringstream os;
    os << std::put_time(&tm, "%Y%m%d-%H%M%S");
    return os.str();
}

std::filesystem::path Rotator::rotatedName() const {
    return dir_ / (base_ + "." + nowStamp() + ext_);
}

// ===== FileLock =====

Rotator::FileLock::FileLock(std::filesystem::path p) : path_(std::move(p)) {
#ifdef __linux__
    fd_ = ::open(path_.c_str(), O_CREAT | O_RDWR, 0644);
    if (fd_ >= 0) {
        if (flock(fd_, LOCK_EX) != 0) {
            ::close(fd_); fd_ = -1;
            throw std::runtime_error("FileLock: flock failed");
        }
    } else {
        throw std::runtime_error("FileLock: open failed");
    }
#endif
}

Rotator::FileLock::~FileLock() {
#ifdef __linux__
    if (fd_ >= 0) {
        flock(fd_, LOCK_UN);
        ::close(fd_);
    }
#endif
}

// ===== rotation/compression/pruning =====

void Rotator::rotateImpl(const RotateReason why) const {
    // Prevent concurrent rotation across processes
    const auto lockfile = *opts_.lock_dir / (base_ + ext_ + ".rotate.lock");
    std::optional<FileLock> lk;
    try { lk.emplace(lockfile); } catch (...) { /* best-effort */ }

    std::error_code ec;
    if (!std::filesystem::exists(opts_.active_path, ec)) return;

    const auto target = rotatedName();

    // Atomic rename of active -> rotated
    std::filesystem::rename(opts_.active_path, target, ec);
    if (ec) {
        if (opts_.diag_log) opts_.diag_log(std::string("rotate: rename failed: ") + ec.message());
        return;
    }

    // Re-open / recreate active file
    if (opts_.on_reopen) {
        try { opts_.on_reopen(); } catch (...) { /* ignore */ }
    } else {
        // Touch new active in case writers open by path without explicit reopen hook
        if (FILE* f = std::fopen(opts_.active_path.c_str(), "ab")) std::fclose(f);
    }

    // Optional compression
    if (opts_.compression != Compression::None) {
        const bool ok = compressFile(target, opts_.compression);
        if (!ok && !opts_.ignore_compress_errors) {
            if (opts_.diag_log) opts_.diag_log("rotate: compression failed (fatal).");
            return;
        }
    }

    if (opts_.diag_log) {
        std::ostringstream os;
        os << "rotate: completed (" << reasonStr(why) << ") -> " << target.filename().string();
        if (opts_.compression == Compression::Gzip) os << ".gz";
        if (opts_.compression == Compression::Zstd) os << ".zst";
        opts_.diag_log(os.str());
    }
}

const char* Rotator::reasonStr(RotateReason r) {
    switch (r) {
        case RotateReason::None: return "none";
        case RotateReason::Size: return "size";
        case RotateReason::Interval: return "interval";
        case RotateReason::Forced: return "forced";
    }
    return "?";
}

bool Rotator::compressFile(const std::filesystem::path& src, Compression c) const {
    std::error_code ec;
    if (!std::filesystem::exists(src, ec)) return false;

    std::string cmd;
    if (c == Compression::Gzip) {
        cmd = "gzip -f -- " + shellQuote(src.string());
    } else if (c == Compression::Zstd) {
        cmd = "zstd -q -f --rm -- " + shellQuote(src.string());
    }
    if (cmd.empty()) return false;

    int rc = std::system(cmd.c_str());
    return (rc == 0);
}

std::string Rotator::shellQuote(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out.push_back('\'');
    for (char ch : s) {
        if (ch == '\'') out += "'\\''";
        else out.push_back(ch);
    }
    out.push_back('\'');
    return out;
}

void Rotator::pruneImpl() const {
    using namespace std::chrono;

    // 1) Gather rotated files
    std::vector<std::filesystem::directory_entry> rotated;
    std::error_code ec;

    for (auto& de : std::filesystem::directory_iterator(dir_, ec)) {
        if (ec) break;
        if (!de.is_regular_file(ec)) continue;
        if (opts_.rotated_filter && opts_.rotated_filter(de.path())) {
            rotated.push_back(de);
        }
    }
    if (rotated.empty()) return;

    // 2) Sort oldest -> newest by mtime
    std::ranges::sort(rotated.begin(), rotated.end(),
              [](const auto& a, const auto& b){
                  std::error_code ea, eb;
                  return std::filesystem::last_write_time(a, ea) < std::filesystem::last_write_time(b, eb);
              });

    // 3) AGE-BASED PRUNE: remove anything strictly older than retention_days
    //    (Compliance floor: files NEWER than threshold are protected here.)
    const auto now = std::chrono::system_clock::now();
    const auto age_floor = now - opts_.retention_days;

    // We’ll rebuild the vector after deletions to keep logic simple.
    auto age_prune = [&](){
        for (auto& de : rotated) {
            std::error_code ec2;
            auto mtime = std::filesystem::last_write_time(de, ec2);
            if (ec2) continue;
            if (to_sys(mtime) < age_floor) {
                std::filesystem::remove(de, ec2);
                if (!ec2 && opts_.diag_log) {
                    opts_.diag_log(std::string("prune: age remove ") + de.path().filename().string());
                }
            }
        }
    };
    age_prune();

    // Rescan survivors
    rotated.clear();
    for (auto& de : std::filesystem::directory_iterator(dir_, ec)) {
        if (ec) break;
        if (!de.is_regular_file(ec)) continue;
        if (opts_.rotated_filter && opts_.rotated_filter(de.path())) {
            rotated.push_back(de);
        }
    }
    if (rotated.empty()) return;

    std::sort(rotated.begin(), rotated.end(),
              [](const auto& a, const auto& b){
                  std::error_code ea, eb;
                  return std::filesystem::last_write_time(a, ea) < std::filesystem::last_write_time(b, eb);
              });

    // 4) SIZE-BASED PRUNE (only if NOT strict_retention)
    //    If strict_retention == true => ignore size cap within the retention window entirely.
    if (!opts_.strict_retention && opts_.max_retained_size) {
        // Compute total size of rotated survivors
        auto total_bytes = uint64_t{0};
        for (auto& de : rotated) {
            std::error_code ec2;
            auto fsz = std::filesystem::file_size(de, ec2);
            if (!ec2) total_bytes += fsz;
        }

        // If we’re over the cap, remove oldest files until we’re under.
        // NOTE: This may delete files that are still within the retention window
        //       (strict_retention=false trades compliance window for disk safety).
        size_t idx = 0;
        while (total_bytes > *opts_.max_retained_size && idx < rotated.size()) {
            std::error_code ec2;
            const auto& victim = rotated[idx];
            auto victim_size = std::filesystem::file_size(victim, ec2);
            if (ec2) { ++idx; continue; }

            std::filesystem::remove(victim, ec2);
            if (!ec2) {
                if (opts_.diag_log) {
                    std::ostringstream os;
                    os << "prune: size remove " << victim.path().filename().string()
                       << " (" << victim_size << "B over cap)";
                    opts_.diag_log(os.str());
                }
                total_bytes = (victim_size > total_bytes) ? 0 : (total_bytes - victim_size);
            }
            ++idx;
        }
    }
}
