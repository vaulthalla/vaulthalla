#pragma once

#include "fs/Fwd.hpp"
#include "protocols/http/Access.hpp"
#include "storage/Fwd.hpp"

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// Folder downloads as a streamed ZIP (#143). Members are STOREd (no compression: most content is already
// compressed, and it keeps the CPU cost at decrypt + CRC-32), with data descriptors carrying the CRC computed while
// the bytes stream, ZIP64 where sizes, offsets or the entry count need it, and UTF-8 names (flag bit 11). With STORE
// and the plaintext sizes known up front, the exact archive length is known before the first byte, so responses
// carry a Content-Length (and HEAD answers without reading anything). Memory stays bounded by one 256 KiB session
// chunk plus at most one small member (kSmallMemberBytes) and the per-entry plan, whatever the archive size.
namespace vh::protocols::http::handler::archive {

// The directory walk (one DB listing per folder, RBAC per entry in memory; ~0.2 ms per entry against a local DB)
// runs before the first byte, again for the HEAD preflight, and its plan (a name and a file row per member, up to
// ~1 KiB) lives until the central directory is written. That walk and that memory are what is bounded, per request:
// ≤ ~50 MiB and seconds of DB work, well inside a reverse proxy's 60 s read timeout. Bytes are not bounded; the
// writer itself handles ZIP64 entry counts.
inline constexpr std::size_t kMaxEntries = 50'000;
// Members up to this size are read in one authenticated pass (one read instead of read + background verify).
inline constexpr uint64_t kSmallMemberBytes = 1ull << 20;

// One archive member, named, sized and authorized before any byte is sent.
struct Member {
    std::string name;                        // safe, '/'-separated, relative; directories end in '/'
    uint64_t size{};                         // plaintext bytes; 0 for directories
    std::time_t mtime{};
    bool directory{false};
    std::shared_ptr<fs::model::File> file;   // null for directories
};

// Opens the plaintext of one file member (production: storage::Engine::openPlaintextReader).
using Opener = std::function<std::shared_ptr<storage::PlaintextReader>(const Member&)>;

// Walks `root` (a directory target) and authorizes every entry exactly as the buffered archive did: humans need
// Read on every file and Read + List on every directory, share recipients need the share's Download (and List for
// folders) on each one; one refusal refuses the whole archive (403), so a partial archive never hides a denial.
// Reads no file bytes. Over kMaxEntries: std::length_error (413).
[[nodiscard]] std::vector<Member> plan(const access::Caller& caller, const access::Target& root);

// The exact length of the stream stream() produces for these members.
[[nodiscard]] uint64_t archiveSize(const std::vector<Member>& members);

// The archive as a sequential reader: read(offset, …) must be called with consecutive offsets (the HTTP session
// and the tests do). Each member is opened only when its turn comes and closed right after; a member whose bytes
// fail authentication, change size or end early throws, so the response is truncated instead of carrying a CRC
// that vouches for bad bytes.
[[nodiscard]] std::shared_ptr<storage::PlaintextReader> stream(std::vector<Member> members, Opener open);

// The archive name for a path relative to the downloaded folder: '/'-joined components, never absolute, never
// containing "." or ".." components (zip-slip), '\' and control bytes and invalid UTF-8 replaced by '_'.
// Throws std::invalid_argument for a path with no usable component.
[[nodiscard]] std::string safeName(std::string_view relativePath, bool directory);

}
