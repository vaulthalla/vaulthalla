#include "email/templates/OperatorTemplates.hpp"

#include <sstream>

namespace vh::email::templates {

std::string escapeHtml(const std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (const char c : value) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

RenderedEmail renderTestEmail(const TestEmailContext& ctx) {
    const auto provider = escapeHtml(ctx.provider);
    const auto instance = escapeHtml(ctx.instance);
    const auto from = escapeHtml(ctx.from);
    const auto recipient = escapeHtml(ctx.recipient);
    const auto mode = ctx.dryRun ? "Dry run" : "Send";

    std::ostringstream html;
    html
        << "<!doctype html><html><body style=\"margin:0;background:#f5f7fb;color:#172033;"
        << "font-family:Inter,Segoe UI,Arial,sans-serif;\">"
        << "<div style=\"max-width:640px;margin:0 auto;padding:32px 20px;\">"
        << "<div style=\"background:#ffffff;border:1px solid #d9e1ec;border-radius:8px;"
        << "overflow:hidden;\">"
        << "<div style=\"background:#172033;color:#ffffff;padding:20px 24px;\">"
        << "<div style=\"font-size:13px;letter-spacing:0;text-transform:uppercase;"
        << "color:#a7b5c8;\">Vaulthalla operator email</div>"
        << "<h1 style=\"margin:8px 0 0;font-size:24px;line-height:1.25;\">Test email</h1>"
        << "</div>"
        << "<div style=\"padding:24px;\">"
        << "<p style=\"margin:0 0 18px;font-size:15px;line-height:1.6;\">"
        << "This is a rendered operator email preview for " << instance << ".</p>"
        << "<table role=\"presentation\" style=\"width:100%;border-collapse:collapse;"
        << "font-size:14px;\">"
        << "<tr><td style=\"padding:10px 0;color:#5a6778;width:140px;\">Mode</td>"
        << "<td style=\"padding:10px 0;font-weight:600;\">" << mode << "</td></tr>"
        << "<tr><td style=\"padding:10px 0;color:#5a6778;\">Provider</td>"
        << "<td style=\"padding:10px 0;font-weight:600;\">" << provider << "</td></tr>"
        << "<tr><td style=\"padding:10px 0;color:#5a6778;\">From</td>"
        << "<td style=\"padding:10px 0;font-weight:600;\">" << from << "</td></tr>"
        << "<tr><td style=\"padding:10px 0;color:#5a6778;\">Recipient</td>"
        << "<td style=\"padding:10px 0;font-weight:600;\">" << recipient << "</td></tr>"
        << "</table>";

    if (ctx.baseUrl)
        html << "<p style=\"margin:20px 0 0;font-size:14px;line-height:1.6;\">"
             << "<a href=\"" << escapeHtml(*ctx.baseUrl) << "\" style=\"color:#0b62b4;\">Open Vaulthalla</a>"
             << "</p>";

    html
        << "</div></div></div></body></html>";

    std::ostringstream text;
    text << "[Vaulthalla] Test operator email\n\n"
         << "Instance: " << ctx.instance << "\n"
         << "Mode: " << mode << "\n"
         << "Provider: " << ctx.provider << "\n"
         << "From: " << ctx.from << "\n"
         << "Recipient: " << ctx.recipient << "\n";
    if (ctx.baseUrl) text << "URL: " << *ctx.baseUrl << "\n";

    return {
        .subject = "[Vaulthalla] Test operator email",
        .html = html.str(),
        .text = text.str()
    };
}

}
