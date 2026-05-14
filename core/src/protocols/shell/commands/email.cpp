#include "protocols/shell/commands/all.hpp"

#include "config/Registry.hpp"
#include "crypto/secrets/Manager.hpp"
#include "email/Message.hpp"
#include "email/ProviderFactory.hpp"
#include "email/providers/ResendProvider.hpp"
#include "email/providers/SesProvider.hpp"
#include "email/templates/OperatorTemplates.hpp"
#include "identities/User.hpp"
#include "protocols/shell/Router.hpp"
#include "protocols/shell/commands/helpers.hpp"
#include "protocols/shell/util/argsHelpers.hpp"
#include "runtime/Deps.hpp"
#include "usage/include/UsageManager.hpp"

#include <sstream>
#include <unistd.h>
#include <optional>

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

std::optional<CommandResult> confirmOverwrite(const CommandCall& call, const std::string& secretKey, const std::string& label) {
    auto& deps = runtime::Deps::get();
    if (!deps.secretsManager)
        return invalid("email provider set: secrets manager is unavailable");

    if (deps.secretsManager->hasSecret(secretKey) && !hasKey(call, "yes")) {
        if (!call.io->confirm(label + " is already stored. Overwrite it? [no]", true))
            return invalid("email provider set: overwrite cancelled");
    }

    return std::nullopt;
}

CommandResult handleProviderResendSet(const CommandCall& call) {
    if (auto denied = requireSuperAdmin(call); denied.exit_code != 0) return denied;
    const auto usage = resolveUsage({"email", "provider", "resend", "set"});
    validatePositionals(call, usage);

    if (!call.io)
        return invalid("email provider resend set: hidden prompt requires an interactive CLI session");

    const auto secretKey = ::vh::email::providers::ResendProvider::kApiKeySecret;
    if (auto err = confirmOverwrite(call, secretKey, "A Resend API key"))
        return *err;

    auto& deps = runtime::Deps::get();

    const auto apiKey = call.io->promptSecret("Resend API key:");
    if (apiKey.empty())
        return invalid("email provider resend set: API key cannot be empty");

    deps.secretsManager->setSecret(secretKey, apiKey);
    return ok("Stored Resend API key secret for operator email delivery.");
}

CommandResult handleProviderSesSet(const CommandCall& call) {
    if (auto denied = requireSuperAdmin(call); denied.exit_code != 0) return denied;
    const auto usage = resolveUsage({"email", "provider", "ses", "set"});
    validatePositionals(call, usage);

    if (!call.io)
        return invalid("email provider ses set: hidden prompts require an interactive CLI session");

    auto& deps = runtime::Deps::get();
    if (!deps.secretsManager)
        return invalid("email provider ses set: secrets manager is unavailable");

    const auto accessOnly = hasKey(call, "access");
    const auto secretOnly = hasKey(call, "secret");
    const auto setAccess = (!accessOnly && !secretOnly) || accessOnly;
    const auto setSecret = (!accessOnly && !secretOnly) || secretOnly;

    if (setAccess) {
        if (auto err = confirmOverwrite(call, ::vh::email::providers::SesProvider::kAccessKeySecret, "An SES access key ID"))
            return *err;
    }
    if (setSecret) {
        if (auto err = confirmOverwrite(call, ::vh::email::providers::SesProvider::kSecretKeySecret, "An SES secret access key"))
            return *err;
    }

    std::optional<std::string> accessKey;
    std::optional<std::string> secretKey;

    if (setAccess) {
        accessKey = call.io->promptSecret("SES access key ID:");
        if (accessKey->empty()) return invalid("email provider ses set: access key ID cannot be empty");
    }
    if (setSecret) {
        secretKey = call.io->promptSecret("SES secret access key:");
        if (secretKey->empty()) return invalid("email provider ses set: secret access key cannot be empty");
    }

    if (accessKey)
        deps.secretsManager->setSecret(::vh::email::providers::SesProvider::kAccessKeySecret, *accessKey);
    if (secretKey)
        deps.secretsManager->setSecret(::vh::email::providers::SesProvider::kSecretKeySecret, *secretKey);

    if (accessKey && secretKey) return ok("Stored SES credential secrets for operator email delivery.");
    if (accessKey) return ok("Stored SES access key ID secret for operator email delivery.");
    return ok("Stored SES secret access key for operator email delivery.");
}

CommandResult handleProviderResend(const CommandCall& call) {
    if (call.positionals.empty() || hasKey(call, "help") || hasKey(call, "h"))
        return usage(call.constructFullArgs());

    const auto [sub, subcall] = descend(call);
    if (isCommandMatch({"email", "provider", "resend", "set"}, sub))
        return handleProviderResendSet(subcall);

    return invalid(call.constructFullArgs(), "Unknown email provider resend subcommand: '" + std::string(sub) + "'");
}

CommandResult handleProviderSes(const CommandCall& call) {
    if (call.positionals.empty() || hasKey(call, "help") || hasKey(call, "h"))
        return usage(call.constructFullArgs());

    const auto [sub, subcall] = descend(call);
    if (isCommandMatch({"email", "provider", "ses", "set"}, sub))
        return handleProviderSesSet(subcall);

    return invalid(call.constructFullArgs(), "Unknown email provider ses subcommand: '" + std::string(sub) + "'");
}

CommandResult handleProvider(const CommandCall& call) {
    if (call.positionals.empty() || hasKey(call, "help") || hasKey(call, "h"))
        return usage(call.constructFullArgs());

    const auto [sub, subcall] = descend(call);
    if (isCommandMatch({"email", "provider", "resend"}, sub))
        return handleProviderResend(subcall);
    if (isCommandMatch({"email", "provider", "ses"}, sub))
        return handleProviderSes(subcall);

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
    out << "  ses region: " << emailCfg.ses.region << "\n";
    out << "  ses endpoint: " << ::vh::email::providers::SesProvider::endpointForConfig(emailCfg.ses) << "\n";
    out << "secrets:\n";
    out << "  resend api key: ";
    if (!deps.secretsManager) out << "unavailable";
    else out << (deps.secretsManager->hasSecret(::vh::email::providers::ResendProvider::kApiKeySecret) ? "present" : "missing");
    out << "\n";
    out << "  ses access key id: ";
    if (!deps.secretsManager) out << "unavailable";
    else out << (deps.secretsManager->hasSecret(::vh::email::providers::SesProvider::kAccessKeySecret) ? "present" : "missing");
    out << "\n";
    out << "  ses secret access key: ";
    if (!deps.secretsManager) out << "unavailable";
    else out << (deps.secretsManager->hasSecret(::vh::email::providers::SesProvider::kSecretKeySecret) ? "present" : "missing");
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
    if (emailCfg.enabled && emailCfg.provider == config::EmailProviderKind::Ses && deps.secretsManager
        && (!deps.secretsManager->hasSecret(::vh::email::providers::SesProvider::kAccessKeySecret)
            || !deps.secretsManager->hasSecret(::vh::email::providers::SesProvider::kSecretKeySecret)))
        out << "warning: SES is selected but one or more credential secrets are missing\n";

    return ok(out.str());
}

CommandResult handleTest(const CommandCall& call) {
    if (auto denied = requireSuperAdmin(call); denied.exit_code != 0) return denied;
    const auto usageModel = resolveUsage({"email", "test"});
    const auto& cfg = config::Registry::get();

    const auto dryRun = hasKey(call, "dry-run");
    const auto send = hasKey(call, "send");
    if (dryRun == send)
        return invalid("email test: specify exactly one of --dry-run or --send");

    const auto recipientValue = optVal(call, usageModel->resolveOptional("to")->option_tokens);
    std::string recipient = recipientValue.value_or("");
    if (dryRun && recipient.empty() && !cfg.operator_emails.recipients.alerts.empty())
        recipient = cfg.operator_emails.recipients.alerts.front();
    if (recipient.empty())
        return invalid(send
            ? "email test --send: pass --to <email>"
            : "email test --dry-run: no recipient configured; pass --to <email>");

    try {
        const auto from = ::vh::email::parseAddress(cfg.email.from);
        const auto to = ::vh::email::parseAddress(recipient);
        const auto rendered = ::vh::email::templates::renderTestEmail({
            .provider = config::emailProviderKindToString(cfg.email.provider),
            .instance = instanceName(),
            .from = ::vh::email::formatAddress(from),
            .recipient = ::vh::email::formatAddress(to),
            .dryRun = dryRun,
            .baseUrl = cfg.email.base_url
        });

        ::vh::email::Message message{
            .from = from,
            .to = {to},
            .replyTo = cfg.email.reply_to ? std::optional(::vh::email::parseAddress(*cfg.email.reply_to)) : std::nullopt,
            .subject = rendered.subject,
            .html = rendered.html,
            .text = rendered.text,
            .idempotencyKey = dryRun ? "operator-test:dry-run" : "operator-test:manual",
            .tags = {}
        };
        ::vh::email::validateMessage(message);

        if (send) {
            if (!cfg.email.enabled) return invalid("email test --send: email.enabled is false");
            if (cfg.email.provider == config::EmailProviderKind::None)
                return invalid("email test --send: email.provider is none");
            if (!runtime::Deps::get().secretsManager)
                return invalid("email test --send: secrets manager is unavailable");

            auto provider = ::vh::email::makeProvider(cfg.email, runtime::Deps::get().secretsManager);
            if (!provider) return invalid("email test --send: no provider is configured");

            const auto result = provider->send(message);
            if (!result.ok) {
                std::ostringstream err;
                err << "email test --send: provider " << provider->name() << " failed";
                if (result.httpStatus) err << " (HTTP " << result.httpStatus << ")";
                if (result.errorSummary) err << ": " << *result.errorSummary;
                return invalid(err.str());
            }

            std::ostringstream out;
            out << "sent test operator email\n";
            out << "provider: " << provider->name() << "\n";
            out << "to: " << ::vh::email::formatAddress(to) << "\n";
            if (result.providerMessageId)
                out << "provider message id: " << *result.providerMessageId << "\n";
            return ok(out.str());
        }

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
