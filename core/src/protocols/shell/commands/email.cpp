#include "protocols/shell/commands/all.hpp"

#include "config/Registry.hpp"
#include "crypto/secrets/Manager.hpp"
#include "email/Message.hpp"
#include "email/providers/ResendProvider.hpp"
#include "email/templates/OperatorTemplates.hpp"
#include "identities/User.hpp"
#include "protocols/shell/Router.hpp"
#include "protocols/shell/commands/helpers.hpp"
#include "protocols/shell/util/argsHelpers.hpp"
#include "runtime/Deps.hpp"
#include "usage/include/UsageManager.hpp"

#include <sstream>
#include <unistd.h>

namespace vh::protocols::shell::commands {

namespace {

std::string yesNo(const bool value) {
    return value ? "yes" : "no";
}

std::string instanceName() {
    char host[256]{};
    if (::gethostname(host, sizeof(host) - 1) == 0 && host[0] != '\0')
        return host;
    return "vaulthalla";
}

CommandResult requireSuperAdmin(const CommandCall& call) {
    if (call.user && call.user->isSuperAdmin()) return {};
    return invalid("email: insufficient permissions; requires super-admin");
}

CommandResult handleProviderResendSet(const CommandCall& call) {
    if (auto denied = requireSuperAdmin(call); denied.exit_code != 0) return denied;
    const auto usage = resolveUsage({"email", "provider", "resend", "set"});
    validatePositionals(call, usage);

    if (!call.io)
        return invalid("email provider resend set: hidden prompt requires an interactive CLI session");

    auto& deps = runtime::Deps::get();
    if (!deps.secretsManager)
        return invalid("email provider resend set: secrets manager is unavailable");

    const auto secretKey = ::vh::email::providers::ResendProvider::kApiKeySecret;
    if (deps.secretsManager->hasSecret(secretKey) && !hasKey(call, "yes")) {
        if (!call.io->confirm("A Resend API key is already stored. Overwrite it? [no]", true))
            return invalid("email provider resend set: overwrite cancelled");
    }

    const auto apiKey = call.io->promptSecret("Resend API key:");
    if (apiKey.empty())
        return invalid("email provider resend set: API key cannot be empty");

    deps.secretsManager->setSecret(secretKey, apiKey);
    return ok("Stored Resend API key secret for operator email delivery.");
}

CommandResult handleProviderResend(const CommandCall& call) {
    if (call.positionals.empty() || hasKey(call, "help") || hasKey(call, "h"))
        return usage(call.constructFullArgs());

    const auto [sub, subcall] = descend(call);
    if (isCommandMatch({"email", "provider", "resend", "set"}, sub))
        return handleProviderResendSet(subcall);

    return invalid(call.constructFullArgs(), "Unknown email provider resend subcommand: '" + std::string(sub) + "'");
}

CommandResult handleProvider(const CommandCall& call) {
    if (call.positionals.empty() || hasKey(call, "help") || hasKey(call, "h"))
        return usage(call.constructFullArgs());

    const auto [sub, subcall] = descend(call);
    if (isCommandMatch({"email", "provider", "resend"}, sub))
        return handleProviderResend(subcall);

    return invalid(call.constructFullArgs(), "Unknown email provider subcommand: '" + std::string(sub) + "'");
}

CommandResult handleDoctor(const CommandCall& call) {
    if (auto denied = requireSuperAdmin(call); denied.exit_code != 0) return denied;
    const auto& cfg = config::Registry::get();
    const auto& emailCfg = cfg.email;
    const auto& operatorCfg = cfg.operator_emails;
    const auto& deps = runtime::Deps::get();

    std::ostringstream out;
    out << "vh email doctor\n";
    out << "email:\n";
    out << "  enabled: " << yesNo(emailCfg.enabled) << "\n";
    out << "  provider: " << config::emailProviderKindToString(emailCfg.provider) << "\n";
    out << "  from: " << emailCfg.from << "\n";
    out << "  reply-to: " << (emailCfg.reply_to ? *emailCfg.reply_to : "none") << "\n";
    out << "  base-url: " << (emailCfg.base_url ? *emailCfg.base_url : "none") << "\n";
    out << "  resend endpoint: " << emailCfg.resend.endpoint << "\n";
    out << "secrets:\n";
    out << "  resend api key: ";
    if (!deps.secretsManager) out << "unavailable";
    else out << (deps.secretsManager->hasSecret(::vh::email::providers::ResendProvider::kApiKeySecret) ? "present" : "missing");
    out << "\n";
    out << "operator emails:\n";
    out << "  enabled: " << yesNo(operatorCfg.enabled) << "\n";
    out << "  alert recipients: " << operatorCfg.recipients.alerts.size() << "\n";
    out << "  weekly recipients: " << operatorCfg.recipients.weekly.size() << "\n";
    out << "  security recipients: " << operatorCfg.recipients.security.size() << "\n";

    if (!emailCfg.enabled && emailCfg.provider != config::EmailProviderKind::None)
        out << "warning: provider is configured but email.enabled is false\n";
    if (operatorCfg.enabled && operatorCfg.recipients.alerts.empty())
        out << "warning: operator emails are enabled but no alert recipients are configured\n";
    if (emailCfg.enabled && emailCfg.provider == config::EmailProviderKind::Resend && deps.secretsManager
        && !deps.secretsManager->hasSecret(::vh::email::providers::ResendProvider::kApiKeySecret))
        out << "warning: Resend is selected but the API key secret is missing\n";
    if (emailCfg.enabled && emailCfg.provider == config::EmailProviderKind::Ses)
        out << "warning: SES provider support is planned for phase 2\n";

    return ok(out.str());
}

CommandResult handleTest(const CommandCall& call) {
    if (auto denied = requireSuperAdmin(call); denied.exit_code != 0) return denied;
    const auto usageModel = resolveUsage({"email", "test"});
    const auto& cfg = config::Registry::get();

    if (!hasKey(call, "dry-run"))
        return invalid("email test: phase 1 supports --dry-run only");

    const auto recipientValue = optVal(call, usageModel->resolveOptional("to")->option_tokens);
    std::string recipient = recipientValue.value_or("");
    if (recipient.empty() && !cfg.operator_emails.recipients.alerts.empty())
        recipient = cfg.operator_emails.recipients.alerts.front();
    if (recipient.empty())
        return invalid("email test --dry-run: no recipient configured; pass --to <email>");

    try {
        const auto from = ::vh::email::parseAddress(cfg.email.from);
        const auto to = ::vh::email::parseAddress(recipient);
        const auto rendered = ::vh::email::templates::renderTestEmail({
            .provider = config::emailProviderKindToString(cfg.email.provider),
            .instance = instanceName(),
            .from = ::vh::email::formatAddress(from),
            .recipient = ::vh::email::formatAddress(to),
            .dryRun = true,
            .baseUrl = cfg.email.base_url
        });

        ::vh::email::Message message{
            .from = from,
            .to = {to},
            .replyTo = cfg.email.reply_to ? std::optional(::vh::email::parseAddress(*cfg.email.reply_to)) : std::nullopt,
            .subject = rendered.subject,
            .html = rendered.html,
            .text = rendered.text,
            .idempotencyKey = "operator-test:dry-run",
            .tags = {}
        };
        ::vh::email::validateMessage(message);

        std::ostringstream out;
        out << "dry-run: rendered test operator email\n";
        out << "subject: " << rendered.subject << "\n";
        out << "to: " << ::vh::email::formatAddress(to) << "\n";
        out << "html bytes: " << rendered.html.size() << "\n";
        out << "\n";
        out << rendered.text;
        return ok(out.str());
    } catch (const std::exception& e) {
        return invalid("email test --dry-run: " + std::string(e.what()));
    }
}

CommandResult handleEmail(const CommandCall& call) {
    if (call.positionals.empty() || hasKey(call, "help") || hasKey(call, "h"))
        return usage(call.constructFullArgs());

    const auto [sub, subcall] = descend(call);
    if (isCommandMatch({"email", "provider"}, sub)) return handleProvider(subcall);
    if (isCommandMatch({"email", "doctor"}, sub)) return handleDoctor(subcall);
    if (isCommandMatch({"email", "test"}, sub)) return handleTest(subcall);

    return invalid(call.constructFullArgs(), "Unknown email subcommand: '" + std::string(sub) + "'");
}

}

void registerEmailCommands(const std::shared_ptr<Router>& r) {
    const auto usageManager = runtime::Deps::get().shellUsageManager;
    r->registerCommand(usageManager->resolve("email"), handleEmail);
}

}
