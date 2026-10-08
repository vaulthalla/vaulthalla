#pragma once

#include "common/protocol.hpp"

#include <nlohmann/json.hpp>

namespace vh::media {

// Each command returns the result JSON (runMain adds ok/output_bytes/sandbox and writes it to fd 4).
nlohmann::json probe(const helpers::Args& args, helpers::RangeClient& range, helpers::OutputSink& sink);
nlohmann::json poster(const helpers::Args& args, helpers::RangeClient& range, helpers::OutputSink& sink);
nlohmann::json transcode(const helpers::Args& args, helpers::RangeClient& range, helpers::OutputSink& sink);
nlohmann::json hls(const helpers::Args& args, helpers::RangeClient& range, helpers::OutputSink& sink);
nlohmann::json capabilities(const helpers::Args& args, helpers::RangeClient& range, helpers::OutputSink& sink);

}
