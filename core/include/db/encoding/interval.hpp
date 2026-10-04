#pragma once

#include <chrono>
#include <string>

namespace vh::db::encoding {

std::chrono::seconds parsePostgresInterval(const std::string& s);

std::string intervalToString(const std::chrono::seconds& interval);

// "300", "30s", "5m", "2h", "1d" (CLI and ws sync interval input).
std::chrono::seconds parseSyncInterval(const std::string& intervalStr);

}
