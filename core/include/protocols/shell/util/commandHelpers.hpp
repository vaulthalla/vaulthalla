#pragma once

#include "protocols/shell/ExecResult.hpp"

#include <string>

namespace vh::protocols::shell::util {

std::string trim(std::string s);
std::string shellQuote(const std::string& s);

ExecResult runCapture(const std::string& command);
bool commandExists(const std::string& command);

bool hasEffectiveRoot();
bool canUsePasswordlessSudo();
bool hasRootOrEquivalentPrivileges();
std::string privilegedPrefix();

std::string formatFailure(const std::string& step, const ExecResult& result);

}
