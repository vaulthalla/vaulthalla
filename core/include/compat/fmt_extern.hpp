#pragma once

// Stop every translation unit from instantiating fmt's formatting engine.
//
// spdlog's logger::log_() calls fmt::vformat_to(appender, ...), which instantiates the (non-inline) function template
// fmt::detail::vformat_to<char> and, through it, the whole float/int writing machinery (dragonbox, bigint, ...).
// The shared libfmt (format.cc) already carries an explicit instantiation of exactly this specialization, so an
// explicit instantiation *declaration* lets every TU link against that one copy instead of compiling its own.
// fmt 10+ declares this itself; only fmt 9 (Ubuntu 24.04's libfmt-dev) needs it.

#include <fmt/format.h>

#if !defined(FMT_HEADER_ONLY) && FMT_VERSION >= 90000 && FMT_VERSION < 100000
FMT_BEGIN_NAMESPACE
namespace detail {
extern template void vformat_to<char>(buffer<char>&, basic_string_view<char>,
                                      basic_format_args<FMT_BUFFER_CONTEXT(type_identity_t<char>)>, locale_ref);
}  // namespace detail
FMT_END_NAMESPACE
#endif
