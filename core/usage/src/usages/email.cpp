#include "usages.hpp"

using namespace vh::protocols::shell;

namespace vh::protocols::shell::email {

namespace {

std::shared_ptr<CommandUsage> baseUsage(const std::weak_ptr<CommandUsage>& parent) {
    const auto cmd = std::make_shared<CommandUsage>();
    cmd->parent = parent;
    return cmd;
}

std::shared_ptr<CommandUsage> providerResendSet(const std::weak_ptr<CommandUsage>& parent) {
    const auto cmd = baseUsage(parent);
    cmd->aliases = {"set"};
    cmd->description = "Set the encrypted Resend API key using a hidden prompt.";
    cmd->optional_flags = {
        Flag::Alias("api_key", "Prompt for the Resend API key", "api-key")
    };
    cmd->examples = {
        {"vh email provider resend set", "Prompt for and store the Resend API key."},
        {"vh email provider resend set --api-key", "Explicitly prompt for and store the Resend API key."}
    };
    return cmd;
}

std::shared_ptr<CommandUsage> providerResend(const std::weak_ptr<CommandUsage>& parent) {
    const auto cmd = baseUsage(parent);
    cmd->aliases = {"resend"};
    cmd->description = "Manage Resend email provider credentials.";
    const auto setCmd = providerResendSet(cmd->weak_from_this());
    cmd->subcommands = {setCmd};
    cmd->examples = {
        {"vh email provider resend set", "Prompt for and store the Resend API key."}
    };
    return cmd;
}

std::shared_ptr<CommandUsage> provider(const std::weak_ptr<CommandUsage>& parent) {
    const auto cmd = baseUsage(parent);
    cmd->aliases = {"provider"};
    cmd->description = "Manage operator email provider credentials.";
    const auto resendCmd = providerResend(cmd->weak_from_this());
    cmd->subcommands = {resendCmd};
    return cmd;
}

std::shared_ptr<CommandUsage> doctor(const std::weak_ptr<CommandUsage>& parent) {
    const auto cmd = baseUsage(parent);
    cmd->aliases = {"doctor"};
    cmd->description = "Inspect operator email configuration without exposing secrets.";
    cmd->examples = {
        {"vh email doctor", "Show configured provider, sender, recipient counts, and secret presence."}
    };
    return cmd;
}

std::shared_ptr<CommandUsage> test(const std::weak_ptr<CommandUsage>& parent) {
    const auto cmd = baseUsage(parent);
    cmd->aliases = {"test"};
    cmd->description = "Render a test operator email.";
    cmd->optional_flags = {
        Flag::Alias("dry_run", "Render and validate without sending", "dry-run")
    };
    cmd->optional = {
        Optional::ManyToOne("to", "Override the test recipient for rendering", {"to"}, "email")
    };
    cmd->examples = {
        {"vh email test --dry-run", "Render the configured test email without sending."},
        {"vh email test --dry-run --to ops@example.com", "Render a test email for a specific recipient."}
    };
    return cmd;
}

}

std::shared_ptr<CommandBook> get(const std::weak_ptr<CommandUsage>& parent) {
    const auto book = std::make_shared<CommandBook>();
    book->title = "Email Commands";
    const auto root = baseUsage(parent);
    root->aliases = {"email"};
    root->description = "Manage operator email configuration and diagnostics.";

    const auto providerCmd = provider(root->weak_from_this());
    const auto doctorCmd = doctor(root->weak_from_this());
    const auto testCmd = test(root->weak_from_this());

    root->subcommands = {
        providerCmd,
        doctorCmd,
        testCmd
    };
    root->examples = {
        {"vh email provider resend set", "Prompt for and store the Resend API key."},
        {"vh email doctor", "Inspect operator email configuration without leaking secrets."},
        {"vh email test --dry-run", "Render the test operator email without sending."}
    };

    book->root = root;
    return book;
}

}
