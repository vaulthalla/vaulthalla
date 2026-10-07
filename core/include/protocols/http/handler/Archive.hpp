#pragma once

#include "protocols/http/Access.hpp"

#include <cstdint>
#include <vector>

// Directory downloads as a stored (uncompressed) ZIP32. Still built in memory, so it keeps its caps (256 MiB of
// source, 320 MiB output, 4096 entries); every child is authorized individually.
namespace vh::protocols::http::handler::archive {

[[nodiscard]] std::vector<uint8_t> build(const access::Caller& caller, const access::Target& root);

}
