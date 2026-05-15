#include "protocols/shell/commands/all.hpp"

#include "config/Registry.hpp"
#include "crypto/secrets/Manager.hpp"
#include "db/encoding/timestamp.hpp"
#include "email/Message.hpp"
#include "email/ProviderFactory.hpp"
#include "email/providers/ResendProvider.hpp"
#include "email/providers/SesProvider.hpp"
#include "email/templates/OperatorTemplates.hpp"
#include "identities/User.hpp"
#include "notifications/OperatorNotification.hpp"
#include "notifications/OperatorNotificationState.hpp"
#include "protocols/shell/Router.hpp"
#include "protocols/shell/commands/helpers.hpp"
#include "protocols/shell/util/argsHelpers.hpp"
#include "runtime/Deps.hpp"
#include "usage/include/UsageManager.hpp"

#include <cstdint>
#include <exception>
#include <optional>
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

std::string valueOrNone(const std::optional<std::string>& value) {
    return value && !value->empty() ? *value : "none";
}

std::string historyWarning(const std::exception& e) {
    return "history warning: " + std::string(e.what());
}

std::optional<std::string> recordDryRun(
    const ::vh::notifications::OperatorNotification& notification,
    const std::string& provider,
    const std::uint32_t recipientCount
) {
    try {
        ::vh::notifications::OperatorNotificationState::recordDryRun(notification, provider, recipientCount);
    } catch (const std::exception& e) {
        return historyWarning(e);
    }
    return std::nullopt;
}

std::optional<std::string> recordSent(
    const ::vh::notifications::OperatorNotification& notification,
    const std::string& provider,
    const std::uint32_t recipientCount,
    const ::vh::email::SendResult& result
) {
    try {
        ::vh::notifications::OperatorNotificationState::recordSent(notification, provider, recipientCount, result);
    } catch (const std::exception& e) {
        return historyWarning(e);
    }
    return std::nullopt;
}

std::optional<std::string> recordFailed(
    const ::vh::notifications::OperatorNotification& notification,
    const std::string& provider,
    const std::uint32_t recipientCount,
    const std::string& error
) {
    try {
        ::vh::notifications::OperatorNotificationState::recordFailed(notification, provider, recipientCount, error);
    } catch (const std::exception& e) {
        return historyWarning(e);
    }
    return std::nullopt;
}

std::optional<std::string> recordSuppressed(
    const ::vh::notifications::OperatorNotification& notification,
    const std::string& provider,
    const std::uint32_t recipientCount,
    const std::string& reason
) {
    try {
        ::vh::notifications::OperatorNotificationState::recordSuppressed(notification, provider, recipientCount, reason);
    } catch (const std::exception& e) {
        return historyWarning(e);
    }
    return std::nullopt;
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
        ::vh::notifications::OperatorNotification notification{
            .eventKey = "operator-test",
            .eventType = "test",
            .severity = "info",
            .recipientGroup = "test",
            .explicitRecipients = {recipient},
            .fingerprint = "manual:" + recipient,
            .rendered = rendered,
            .tags = {{"event_type", "test"}, {"severity", "info"}}
        };

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
            if (!cfg.email.enabled) {
                recordSuppressed(notification, config::emailProviderKindToString(cfg.email.provider), 1, "email.enabled is false");
                return invalid("email test --send: email.enabled is false");
            }
            if (cfg.email.provider == config::EmailProviderKind::None) {
                recordSuppressed(notification, "none", 1, "email.provider is none");
                return invalid("email test --send: email.provider is none");
            }
            if (!runtime::Deps::get().secretsManager) {
                recordFailed(notification, config::emailProviderKindToString(cfg.email.provider), 1, "secrets manager is unavailable");
                return invalid("email test --send: secrets manager is unavailable");
            }

            auto provider = ::vh::email::makeProvider(cfg.email, runtime::Deps::get().secretsManager);
            if (!provider) {
                recordFailed(notification, config::emailProviderKindToString(cfg.email.provider), 1, "no provider is configured");
                return invalid("email test --send: no provider is configured");
            }

            const auto result = provider->send(message);
            if (!result.ok) {
                std::ostringstream err;
                err << "email test --send: provider " << provider->name() << " failed";
                if (result.httpStatus) err << " (HTTP " << result.httpStatus << ")";
                if (result.errorSummary) err << ": " << *result.errorSummary;
                recordFailed(notification, provider->name(), 1, err.str());
                return invalid(err.str());
            }

            const auto history = recordSent(notification, provider->name(), 1, result);
            std::ostringstream out;
            out << "sent test operator email\n";
            out << "provider: " << provider->name() << "\n";
            out << "to: " << ::vh::email::formatAddress(to) << "\n";
            if (result.providerMessageId)
                out << "provider message id: " << *result.providerMessageId << "\n";
            if (history) out << *history << "\n";
            return ok(out.str());
        }

        const auto history = recordDryRun(notification, config::emailProviderKindToString(cfg.email.provider), 1);
        std::ostringstream out;
        out << "dry-run: rendered test operator email\n";
        out << "subject: " << rendered.subject << "\n";
        out << "to: " << ::vh::email::formatAddress(to) << "\n";
        out << "html bytes: " << rendered.html.size() << "\n";
        if (history) out << *history << "\n";
        out << "\n";
        out << rendered.text;
        return ok(out.str());
    } catch (const std::exception& e) {
        return invalid("email test: " + std::string(e.what()));
    }
}

CommandResult handleHistory(const CommandCall& call) {
    if (auto denied = requireSuperAdmin(call); denied.exit_code != 0) return denied;
    const auto usageModel = resolveUsage({"email", "history"});
    validatePositionals(call, usageModel);

    std::uint32_t limit = 100;
    if (const auto limitOpt = optVal(call, usageModel->resolveOptional("limit")->option_tokens)) {
        const auto parsed = parseUInt(*limitOpt);
        if (!parsed) return invalid("email history: --limit must be a positive integer");
        limit = *parsed;
    }

    try {
        const auto rows = ::vh::notifications::OperatorNotificationState::history(limit);
        std::ostringstream out;
        out << "operator email delivery history\n";
        if (rows.empty()) {
            out << "no delivery records found\n";
            return ok(out.str());
        }

        for (const auto& row : rows) {
            out << db::encoding::timestampToString(row.createdAt)
                << " id=" << row.id
                << " status=" << row.status
                << " event=" << row.eventType
                << " severity=" << row.severity
                << " group=" << valueOrNone(row.recipientGroup)
                << " recipients=" << row.recipientCount
                << " provider=" << row.provider;
            if (row.providerMessageId)
                out << " provider_message_id=" << *row.providerMessageId;
            if (row.errorSummary)
                out << " error=\"" << *row.errorSummary << "\"";
            out << "\n";
        }

        return ok(out.str());
    } catch (const std::exception& e) {
        return invalid("email history: " + std::string(e.what()));
    }
}

CommandResult handleEmail(const CommandCall& call) {
    if (call.positionals.empty() || hasKey(call, "help") || hasKey(call, "h"))
        return usage(call.constructFullArgs());

    const auto [sub, subcall] = descend(call);
    if (isCommandMatch({"email", "provider"}, sub)) return handleProvider(subcall);
    if (isCommandMatch({"email", "doctor"}, sub)) return handleDoctor(subcall);
    if (isCommandMatch({"email", "test"}, sub)) return handleTest(subcall);
    if (isCommandMatch({"email", "history"}, sub)) return handleHistory(subcall);

    return invalid(call.constructFullArgs(), "Unknown email subcommand: '" + std::string(sub) + "'");
}

}

void registerEmailCommands(const std::shared_ptr<Router>& r) {
    const auto usageManager = runtime::Deps::get().shellUsageManager;
    r->registerCommand(usageManager->resolve("email"), handleEmail);
}

}
