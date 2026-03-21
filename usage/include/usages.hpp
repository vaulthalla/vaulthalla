#pragma once

#include "CommandUsage.hpp"
#include "CommandBook.hpp"
#include "permsUtil.hpp"

namespace vh::protocols::shell {

struct CommandCall;

namespace aku { [[nodiscard]] std::string usage_cloud_provider(); }

namespace vault { std::shared_ptr<CommandBook> get(const std::weak_ptr<CommandUsage>& parent); }
namespace user { std::shared_ptr<CommandBook> get(const std::weak_ptr<CommandUsage>& parent); }
namespace group { std::shared_ptr<CommandBook> get(const std::weak_ptr<CommandUsage>& parent); }
namespace secrets { std::shared_ptr<CommandBook> get(const std::weak_ptr<CommandUsage>& parent); }
namespace aku { std::shared_ptr<CommandBook> get(const std::weak_ptr<CommandUsage>& parent); }
namespace role { std::shared_ptr<CommandBook> get(const std::weak_ptr<CommandUsage>& parent); }
namespace permissions { std::shared_ptr<CommandBook> get(const std::weak_ptr<CommandUsage>& parent); }
namespace help { std::shared_ptr<CommandBook> get(const std::weak_ptr<CommandUsage>& parent); }
namespace version { std::shared_ptr<CommandBook> get(const std::weak_ptr<CommandUsage>& parent); }

const auto gpgRecipient = Optional::ManyToOne("gpg_recipient", "GPG fingerprint to encrypt the exported key (if blank will not encrypt)",
                            {"recipient", "r"}, "gpg-fingerprint");

const auto outputFile = Optional::ManyToOne("output", "Output file for the exported key (if blank will print to stdout)",
                            {"output", "o"}, "file");

const auto subjectOption = Option::Multi("subject", "Specify the user or group the command is targeting", {"user", "u", "group", "g"},
                      {"id", "name"});

const auto permissionsFlags = Flag::WithAliases("permissions", "Permission flags to set for the new role (see 'vh permissions') min=1",
                          ALL_SHELL_PERMS_STR);

const auto jsonFlag = Flag::WithAliases("json_output", "Output the list in JSON format", {"json", "j"});

const auto limitOpt = Optional::ManyToOne("limit", "Limit the number of vaults displayed", {"limit", "n"}, "limit");

const auto pageOpt = Optional::ManyToOne("page", "Specify the page number when using --limit for pagination", {"page", "p"}, "page");

const auto userPos = Positional::WithAliases("user", "Username or ID of the user", {"name", "id"});

}
