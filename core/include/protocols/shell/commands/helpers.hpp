#pragma once

#include <memory>
#include <string>
#include <vector>

namespace vh::protocols::shell {

class CommandUsage;
struct CommandCall;

std::shared_ptr<CommandUsage> resolveUsage(const std::vector<std::string>& path);
void validatePositionals(const CommandCall& call, const std::shared_ptr<CommandUsage>& usage);

}
