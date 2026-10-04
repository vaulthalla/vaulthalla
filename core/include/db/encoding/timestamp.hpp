#pragma once

#include <ctime>
#include <string>

namespace vh::db::encoding {

std::time_t parsePostgresTimestamp(const std::string& timestampStr);

std::string timestampToString(std::time_t ts);

std::time_t parseTimestampFromString(const std::string& iso);

std::string getCurrentTimestamp();

std::string getDate();

}
