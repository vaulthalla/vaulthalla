#pragma once

// Hardware encoder selection lives inside the helper (never the daemon). Device contexts are created before the
// sandbox is applied (driver libraries load and render nodes open while the filesystem is still visible); the
// encoders later use the already-open devices. Any failure falls back to software.
//
// VAAPI, QSV and NVENC paths are implemented but unvalidated on real hardware at the time of writing.

#include <nlohmann/json.hpp>

extern "C" {
#include <libavutil/buffer.h>
}

#include <string>
#include <string_view>
#include <vector>

namespace vh::media::hw {

enum class Accel { Software, Vaapi, Qsv, Nvenc };

std::string_view toString(Accel accel);
const char* encoderName(Accel accel);       // libx264, h264_vaapi, h264_qsv, h264_nvenc
bool validHwaccel(std::string_view hwaccel); // auto|software|vaapi|qsv|nvenc

// Pre-sandbox. "auto" prepares every accelerator, "software" none, a vendor name only that one. Never throws.
void prepare(std::string_view hwaccel);

AVBufferRef* device(Accel accel);           // nullptr when unavailable (not prepared, or creation failed)
std::string unavailableReason(Accel accel);

// Ordered encoder candidates for a --hwaccel value; always ends with Software.
std::vector<Accel> candidates(std::string_view hwaccel);

nlohmann::json describe();                  // per-accelerator availability for `capabilities`

}
