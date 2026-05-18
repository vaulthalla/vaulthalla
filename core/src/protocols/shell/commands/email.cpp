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

#include <algorithm>
#include <cctype>
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

std::string lower(std::string value) {
    std::ranges::transform(value, value.begin(), [](const unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::optional<bool> parseBool(const std::string& value) {
    const auto normalized = lower(value);
    if (normalized == "true" || normalized == "yes" || normalized == "on" || normalized == "1" || normalized == "enabled")
        return true;
    if (normalized == "false" || normalized == "no" || normalized == "off" || normalized == "0" || normalized == "disabled")
        return false;
    return std::nullopt;
}

std::optional<std::string> parseOptionalString(const std::string& value) {
    const auto normalized = lower(value);
    if (normalized == "none" || normalized == "null" || normalized == "clear" || normalized == "-")
        return std::nullopt;
    return value;
}

bool validWeekday(const std::string& value) {
    static const std::vector<std::string> days{
        "sunday", "monday", "tuesday", "wednesday", "thursday", "friday", "saturday"
    };
    return std::ranges::find(days, lower(value)) != days.end();
}

bool validSeverity(const std::string& value) {
    static const std::vector<std::string> severities{"info", "warning", "critical"};
    return std::ranges::find(severities, lower(value)) != severities.end();
}

std::vector<std::string>* recipientsForGroup(config::OperatorEmailRecipientsConfig& recipients, const std::string& group) {
    const auto normalized = lower(group);
    if (normalized == "alerts" || normalized == "alert") return &recipients.alerts;
    if (normalized == "weekly" || normalized == "recap" || normalized == "digest") return &recipients.weekly;
    if (normalized == "security" || normalized == "security-alerts") return &recipients.security;
    return nullptr;
}

CommandResult saveEmailConfig(config::Config cfg, const std::string& message) {
    try {
        cfg.save();
        config::Registry::set(cfg);
        return ok(message);
    } catch (const std::exception& e) {
        return invalid("email config: failed to save config: " + std::string(e.what()));
    }
}

std::string recipientsListText(const std::string& group, const std::vector<std::string>& recipients) {
    std::ostringstream out;
    out << group << " recipients\n";
    if (recipients.empty()) {
        out << "none\n";
        return out.str();
    }
    for (const auto& recipient : recipients)
        out << "- " << recipient << "\n";
    return out.str();
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

CommandResult handleProviderUse(const CommandCall& call) {
    if (auto denied = requireSuperAdmin(call); denied.exit_code != 0) return denied;

    if (call.positionals.size() != 1)
        return invalid(call.constructFullArgs(), "email provider use: expected provider: none, resend, or ses");

    try {
        auto cfg = config::Registry::get();
        cfg.email.provider = config::emailProviderKindFromString(call.positionals[0]);
        return saveEmailConfig(
            cfg,
            "Email provider set to " + config::emailProviderKindToString(cfg.email.provider) + ".\n"
        );
    } catch (const std::exception& e) {
        return invalid("email provider use: " + std::string(e.what()));
    }
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
    if (isCommandMatch({"email", "provider", "use"}, sub))
        return handleProviderUse(subcall);

    return invalid(call.constructFullArgs(), "Unknown email provider subcommand: '" + std::string(sub) + "'");
}

CommandResult handleSet(const CommandCall& call) {
    if (auto denied = requireSuperAdmin(call); denied.exit_code != 0) return denied;

    if (call.positionals.size() != 2)
        return invalid(call.constructFullArgs(), "email set: expected <field> <value>");

    auto cfg = config::Registry::get();
    const auto field = lower(call.positionals[0]);
    const auto& value = call.positionals[1];

    try {
        if (field == "enabled") {
            const auto parsed = parseBool(value);
            if (!parsed) return invalid("email set enabled: value must be true or false");
            cfg.email.enabled = *parsed;
        } else if (field == "operator-enabled" || field == "operator_emails.enabled") {
            const auto parsed = parseBool(value);
            if (!parsed) return invalid("email set operator-enabled: value must be true or false");
            cfg.operator_emails.enabled = *parsed;
        } else if (field == "from") {
            (void)::vh::email::parseAddress(value);
            cfg.email.from = value;
        } else if (field == "reply-to" || field == "reply_to") {
            if (const auto parsed = parseOptionalString(value)) {
                (void)::vh::email::parseAddress(*parsed);
                cfg.email.reply_to = *parsed;
            } else cfg.email.reply_to.reset();
        } else if (field == "base-url" || field == "base_url") {
            cfg.email.base_url = parseOptionalString(value);
        } else if (field == "resend-endpoint" || field == "resend.endpoint") {
            if (value.empty()) return invalid("email set resend-endpoint: value cannot be empty");
            cfg.email.resend.endpoint = value;
        } else if (field == "ses-region" || field == "ses.region") {
            if (value.empty()) return invalid("email set ses-region: value cannot be empty");
            cfg.email.ses.region = value;
        } else if (field == "ses-endpoint" || field == "ses.endpoint") {
            cfg.email.ses.endpoint = parseOptionalString(value);
        } else {
            return invalid("email set: unknown field '" + call.positionals[0] + "'");
        }
    } catch (const std::exception& e) {
        return invalid("email set " + call.positionals[0] + ": " + std::string(e.what()));
    }

    return saveEmailConfig(cfg, "Updated email setting " + call.positionals[0] + ".\n");
}

CommandResult handleRecipients(const CommandCall& call) {
    if (auto denied = requireSuperAdmin(call); denied.exit_code != 0) return denied;

    if (call.positionals.size() < 2)
        return invalid(call.constructFullArgs(), "email recipients: expected <alerts|weekly|security> <list|add|remove> [email]");

    auto cfg = config::Registry::get();
    auto* recipients = recipientsForGroup(cfg.operator_emails.recipients, call.positionals[0]);
    if (!recipients)
        return invalid("email recipients: unknown group '" + call.positionals[0] + "'");

    const auto action = lower(call.positionals[1]);
    if (action == "list") {
        if (call.positionals.size() != 2)
            return invalid(call.constructFullArgs(), "email recipients list: expected no email argument");
        return ok(recipientsListText(call.positionals[0], *recipients));
    }

    if (call.positionals.size() != 3)
        return invalid(call.constructFullArgs(), "email recipients " + action + ": expected <email>");

    const auto& recipient = call.positionals[2];
    try {
        (void)::vh::email::parseAddress(recipient);
    } catch (const std::exception& e) {
        return invalid("email recipients " + action + ": " + std::string(e.what()));
    }

    if (action == "add") {
        if (std::ranges::find(*recipients, recipient) == recipients->end())
            recipients->push_back(recipient);
        return saveEmailConfig(cfg, "Added " + recipient + " to " + call.positionals[0] + " recipients.\n");
    }
    if (action == "remove" || action == "delete") {
        const auto before = recipients->size();
        std::erase(*recipients, recipient);
        if (before == recipients->size())
            return invalid("email recipients remove: recipient is not configured for " + call.positionals[0]);
        return saveEmailConfig(cfg, "Removed " + recipient + " from " + call.positionals[0] + " recipients.\n");
    }

    return invalid("email recipients: unknown action '" + call.positionals[1] + "'");
}

CommandResult handleWeekly(const CommandCall& call) {
    if (auto denied = requireSuperAdmin(call); denied.exit_code != 0) return denied;
    if (call.positionals.empty())
        return invalid(call.constructFullArgs(), "email weekly: expected show or set");

    auto cfg = config::Registry::get();
    auto& weekly = cfg.operator_emails.weekly_digest;
    const auto action = lower(call.positionals[0]);

    if (action == "show") {
        std::ostringstream out;
        out << "weekly digest\n";
        out << "  enabled: " << yesNo(weekly.enabled) << "\n";
        out << "  weekday: " << weekly.weekday << "\n";
        out << "  hour_local: " << weekly.hour_local << "\n";
        out << "  timezone: " << weekly.timezone << "\n";
        out << "  recipients: " << cfg.operator_emails.recipients.weekly.size() << "\n";
        return ok(out.str());
    }

    if (action != "set" || call.positionals.size() != 3)
        return invalid(call.constructFullArgs(), "email weekly set: expected <enabled|weekday|hour|timezone> <value>");

    const auto field = lower(call.positionals[1]);
    const auto& value = call.positionals[2];
    if (field == "enabled") {
        const auto parsed = parseBool(value);
        if (!parsed) return invalid("email weekly set enabled: value must be true or false");
        weekly.enabled = *parsed;
    } else if (field == "weekday") {
        if (!validWeekday(value)) return invalid("email weekly set weekday: expected sunday through saturday");
        weekly.weekday = lower(value);
    } else if (field == "hour" || field == "hour-local" || field == "hour_local") {
        const auto parsed = parseUInt(value);
        if (!parsed || *parsed > 23) return invalid("email weekly set hour: expected an integer from 0 to 23");
        weekly.hour_local = *parsed;
    } else if (field == "timezone") {
        if (value.empty()) return invalid("email weekly set timezone: value cannot be empty");
        weekly.timezone = value;
    } else {
        return invalid("email weekly set: unknown field '" + call.positionals[1] + "'");
    }

    return saveEmailConfig(cfg, "Updated weekly digest setting " + call.positionals[1] + ".\n");
}

CommandResult handleSecurity(const CommandCall& call) {
    if (auto denied = requireSuperAdmin(call); denied.exit_code != 0) return denied;
    if (call.positionals.empty())
        return invalid(call.constructFullArgs(), "email security: expected show or set");

    auto cfg = config::Registry::get();
    auto& security = cfg.operator_emails.security_alerts;
    const auto action = lower(call.positionals[0]);

    if (action == "show") {
        std::ostringstream out;
        out << "security alerts\n";
        out << "  enabled: " << yesNo(security.enabled) << "\n";
        out << "  admin_role_changes: " << yesNo(security.admin_role_changes) << "\n";
        out << "  recipients: " << cfg.operator_emails.recipients.security.size() << "\n";
        return ok(out.str());
    }

    if (action != "set" || call.positionals.size() != 3)
        return invalid(call.constructFullArgs(), "email security set: expected <enabled|admin-role-changes> <value>");

    const auto field = lower(call.positionals[1]);
    const auto parsed = parseBool(call.positionals[2]);
    if (!parsed) return invalid("email security set: value must be true or false");

    if (field == "enabled") security.enabled = *parsed;
    else if (field == "admin-role-changes" || field == "admin_role_changes") security.admin_role_changes = *parsed;
    else return invalid("email security set: unknown field '" + call.positionals[1] + "'");

    return saveEmailConfig(cfg, "Updated security alert setting " + call.positionals[1] + ".\n");
}

CommandResult handleAlerting(const CommandCall& call) {
    if (auto denied = requireSuperAdmin(call); denied.exit_code != 0) return denied;
    if (call.positionals.empty())
        return invalid(call.constructFullArgs(), "email alerting: expected show or set");

    auto cfg = config::Registry::get();
    auto& alerting = cfg.operator_emails.alerting;
    const auto action = lower(call.positionals[0]);

    if (action == "show") {
        std::ostringstream out;
        out << "alerting policy\n";
        out << "  enabled: " << yesNo(alerting.enabled) << "\n";
        out << "  min_severity: " << alerting.min_severity << "\n";
        out << "  dedupe_window_minutes: " << alerting.dedupe_window_minutes << "\n";
        out << "  repeat_after_hours: " << alerting.repeat_after_hours << "\n";
        out << "  send_recovery: " << yesNo(alerting.send_recovery) << "\n";
        out << "  health_poll_seconds: " << alerting.health_poll_seconds << "\n";
        return ok(out.str());
    }

    if (action != "set" || call.positionals.size() != 3)
        return invalid(call.constructFullArgs(), "email alerting set: expected <field> <value>");

    const auto field = lower(call.positionals[1]);
    const auto& value = call.positionals[2];
    if (field == "enabled") {
        const auto parsed = parseBool(value);
        if (!parsed) return invalid("email alerting set enabled: value must be true or false");
        alerting.enabled = *parsed;
    } else if (field == "min-severity" || field == "min_severity") {
        if (!validSeverity(value)) return invalid("email alerting set min-severity: expected info, warning, or critical");
        alerting.min_severity = lower(value);
    } else if (field == "dedupe-window-minutes" || field == "dedupe_window_minutes") {
        const auto parsed = parseUInt(value);
        if (!parsed || *parsed < 1) return invalid("email alerting set dedupe-window-minutes: expected a positive integer");
        alerting.dedupe_window_minutes = *parsed;
    } else if (field == "repeat-after-hours" || field == "repeat_after_hours") {
        const auto parsed = parseUInt(value);
        if (!parsed || *parsed < 1) return invalid("email alerting set repeat-after-hours: expected a positive integer");
        alerting.repeat_after_hours = *parsed;
    } else if (field == "send-recovery" || field == "send_recovery") {
        const auto parsed = parseBool(value);
        if (!parsed) return invalid("email alerting set send-recovery: value must be true or false");
        alerting.send_recovery = *parsed;
    } else if (field == "health-poll-seconds" || field == "health_poll_seconds") {
        const auto parsed = parseUInt(value);
        if (!parsed || *parsed < 15) return invalid("email alerting set health-poll-seconds: expected an integer of at least 15");
        alerting.health_poll_seconds = *parsed;
    } else {
        return invalid("email alerting set: unknown field '" + call.positionals[1] + "'");
    }

    return saveEmailConfig(cfg, "Updated alerting setting " + call.positionals[1] + ".\n");
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
    if (isCommandMatch({"email", "set"}, sub)) return handleSet(subcall);
    if (isCommandMatch({"email", "recipients"}, sub)) return handleRecipients(subcall);
    if (isCommandMatch({"email", "weekly"}, sub)) return handleWeekly(subcall);
    if (isCommandMatch({"email", "security"}, sub)) return handleSecurity(subcall);
    if (isCommandMatch({"email", "alerting"}, sub)) return handleAlerting(subcall);
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
