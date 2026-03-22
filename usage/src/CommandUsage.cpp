#include "CommandUsage.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>
#include <variant>
#include <ranges>
#include <optional>
#include <functional>

#include <fmt/format.h>

namespace vh::protocols::shell {

// ======================================================
// helpers (anonymous namespace)
// ======================================================

namespace {

std::string trimRight(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}

std::vector<std::string> wrap(const std::string& s, int width) {
    const int W = std::max(20, width);
    std::vector<std::string> out;
    std::size_t i = 0, n = s.size();
    while (i < n) {
        // skip leading spaces
        while (i < n && std::isspace(static_cast<unsigned char>(s[i])) && s[i] != '\n') ++i;

        // hard break at newline
        if (i < n && s[i] == '\n') {
            out.emplace_back("");
            ++i;
            continue;
        }

        if (i >= n) break;

        const std::size_t end = std::min<std::size_t>(i + W, n);
        std::size_t break_pos = end;

        // prefer last space before end
        if (end < n && s[end] != ' ') {
            auto sp = s.rfind(' ', end);
            if (sp != std::string::npos && sp >= i) break_pos = sp;
        }

        if (break_pos == i) break_pos = end; // no space found

        std::string line = trimRight(s.substr(i, break_pos - i));
        out.push_back(std::move(line));

        // move i
        if (break_pos < n && s[break_pos] == ' ') i = break_pos + 1;
        else i = break_pos;
    }
    if (out.empty()) out.emplace_back("");
    return out;
}

void emitWrapped(std::ostringstream& out, const std::string& text,
                 std::size_t indent, int width) {
    const auto lines = wrap(text, width - static_cast<int>(indent));
    for (const auto& ln : lines) {
        out << std::string(indent, ' ') << ln << "\n";
    }
}

std::string padRight(const std::string& s, std::size_t width) {
    if (s.size() >= width) return s;
    return s + std::string(width - s.size(), ' ');
}

std::string dashifyToken(const std::string& t) {
    if (t.empty()) return t;
    return (t.size() == 1 ? "-" : "--") + t;
}

std::string join(const std::vector<std::string>& v, std::string_view sep) {
    std::ostringstream ss;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) ss << sep;
        ss << v[i];
    }
    return ss.str();
}

std::vector<std::string> dashifyTokens(const std::vector<std::string>& toks) {
    std::vector<std::string> out;
    out.reserve(toks.size());
    for (auto& t : toks) out.push_back(dashifyToken(t));
    return out;
}

std::string angleChoice(const std::vector<std::string>& choices) {
    if (choices.empty()) return std::string{};
    return fmt::format("<{}>", join(choices, " | "));
}

std::string parenChoice(const std::vector<std::string>& choices) {
    if (choices.empty()) return std::string{};
    if (choices.size() == 1) return choices.front();
    return fmt::format("({})", join(choices, " | "));
}

struct KeyDesc {
    std::string key;
    std::string desc;
};

// Escape pipes for Markdown table cells
std::string escapePipes(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) out += (c == '|' ? "\\|" : std::string(1, c));
    return out;
}

template <class T, class F>
std::size_t visibleKeyWidth(const std::vector<T>& items,
                            std::size_t cap,
                            F&& render) {
    std::size_t w = 0;
    for (const auto& it : items) {
        auto kd = render(it);
        w = std::max<std::size_t>(w, kd.key.size());
    }
    return std::min(w, cap);
}

template <class T, class F>
void emitTwoCol(std::ostringstream& out,
                const std::vector<T>& items,
                std::size_t indent, std::size_t gap,
                int width, std::size_t keyw,
                const ColorTheme& theme,
                F&& render) {
    if (items.empty()) return;
    const int rightw = width - static_cast<int>(indent + keyw + gap);
    for (const auto& it : items) {
        auto kd = render(it);
        const auto desc_lines = wrap(kd.desc, std::max(20, rightw));

        out << std::string(indent, ' ');
        out << theme.K() << padRight(kd.key, keyw) << theme.R();
        out << std::string(gap, ' ');

        if (!desc_lines.empty()) {
            out << desc_lines[0] << "\n";
            for (std::size_t i = 1; i < desc_lines.size(); ++i) {
                out << std::string(indent + keyw + gap, ' ') << desc_lines[i] << "\n";
            }
        } else out << "\n";
    }
}

template <class T, class F>
void emitTwoColSection(std::ostringstream& out,
                       const std::string& title,
                       const std::vector<T>& items,
                       std::size_t indent, std::size_t gap,
                       int width, std::size_t max_key_col,
                       const ColorTheme& theme,
                       F&& render) {
    if (items.empty()) return;
    out << theme.H() << title << theme.R() << "\n";
    const auto keyw = visibleKeyWidth(items, max_key_col, render);
    emitTwoCol(out, items, indent, gap, width, keyw, theme, render);
    out << "\n";
}

template <class T, class F>
void emitMarkdownTable(std::ostringstream& md,
                       const std::vector<T>& items,
                       F&& render) {
    if (items.empty()) return;
    md << "| Option | Description |\n";
    md << "|:------ |:----------- |\n";
    for (const auto& it : items) {
        auto kd = render(it);
        md << "| `" << escapePipes(kd.key) << "` | " << escapePipes(kd.desc) << " |\n";
    }
}

// ---------- formatting for each type ----------

KeyDesc renderPositional(const Positional& p, bool show_aliases) {
    std::vector<std::string> names;
    names.push_back(p.label);
    for (auto& a : p.aliases) names.push_back(a);
    return { angleChoice(show_aliases ? names : std::vector<std::string>{names.front()}), p.desc };
}

KeyDesc renderFlag(const Flag& f, bool show_aliases) {
    std::vector<std::string> toks = f.aliases.empty() ? std::vector<std::string>{f.label} : f.aliases;
    auto dash = dashifyTokens(toks);
    std::string key = show_aliases ? parenChoice(dash) : dash.front();
    // Hint default
    std::string d = f.desc;
    d += fmt::format(" (default: {})", f.default_state ? "on" : "off");
    return { key, d };
}

KeyDesc renderOption(const Option& o, bool show_aliases) {
    std::vector<std::string> toks = o.option_tokens.empty() ? std::vector<std::string>{o.label} : o.option_tokens;
    auto dash = dashifyTokens(toks);
    const std::string K = show_aliases ? parenChoice(dash) : dash.front();

    std::string V;
    if (!o.value_tokens.empty()) V = " " + angleChoice(o.value_tokens);

    return { K + V, o.desc };
}

KeyDesc renderOptional(const Optional& o, bool show_aliases) {
    auto kd = renderOption(o, show_aliases);
    if (o.default_value && !o.default_value->empty()) {
        kd.desc += fmt::format(" (default: {})", *o.default_value);
    }
    return kd;
}

KeyDesc renderGrouped(const std::variant<Optional, Flag>& v, bool show_aliases) {
    return std::visit([&](auto&& x)->KeyDesc {
        using T = std::decay_t<decltype(x)>;
        if constexpr (std::is_same_v<T, Optional>) return renderOptional(x, show_aliases);
        else return renderFlag(x, show_aliases);
    }, v);
}

// Escape-aware line break
static std::string lineBreak(int tw = 100, int padding = 3) {
    if (tw - 2 * padding < 10) return std::string(tw, '-');
    return std::string(padding, ' ') + std::string(tw - 2 * padding, '-') + std::string(padding, ' ');
}

// synopsis helpers
std::string synopsisTokenList(const std::vector<std::string>& tokens, bool show_aliases) {
    if (tokens.empty()) return {};
    auto dash = dashifyTokens(tokens);
    return show_aliases ? parenChoice(dash) : dash.front();
}

} // anonymous

// ======================================================
// CommandUsage internals
// ======================================================

bool CommandUsage::matches(const std::string& alias) const {
    return std::ranges::any_of(aliases, [&](const std::string& a){ return a == alias; });
}

std::string CommandUsage::primary() const {
    if (aliases.empty()) throw std::runtime_error("CommandUsage::primary() called with no aliases");
    return aliases[0];
}

std::string CommandUsage::joinAliasesInline_(const std::string& sep) const {
    if (aliases.empty()) throw std::runtime_error("joinAliasesInline_ called with empty aliases");
    std::ostringstream ss;
    if (aliases.size() > 1 || pluralAliasImpliesList) ss << "[";
    for (unsigned int i = 0; i < aliases.size(); ++i) {
        ss << aliases[i];
        if (i + 1 < aliases.size()) ss << sep;
    }
    if (pluralAliasImpliesList) ss << sep << primary() << "s";
    if (aliases.size() > 1 || pluralAliasImpliesList) ss << "]";
    return ss.str();
}

std::string CommandUsage::joinAliasesCode_() {
    if (aliases.empty()) return fmt::format("`{}`", primary());
    std::ostringstream ss;
    ss << "`" << primary() << "` (";
    for (std::size_t i = 0; i < aliases.size(); ++i) {
        if (i) ss << ", ";
        ss << "`" << aliases[i] << "`";
    }
    if (pluralAliasImpliesList) ss << ", `" << primary() << "s`";
    ss << ")";
    return ss.str();
}

std::string CommandUsage::constructCmdString() const {
    std::ostringstream ss;
    if (const auto p = parent.lock(); p.get()) ss << p->constructCmdString() << " ";
    const auto cmdWithAliases = show_aliases ? joinAliasesInline_() : primary();
    ss << cmdWithAliases;
    return ss.str();
}

std::vector<const CommandUsage*> CommandUsage::lineage_() const {
    std::vector<const CommandUsage*> chain;
    const CommandUsage* cur = this;
    while (cur) {
        chain.push_back(cur);
        auto p = cur->parent.lock();
        cur = p.get();
    }
    std::ranges::reverse(chain.begin(), chain.end());
    return chain;
}

std::string CommandUsage::tokenFor_(const CommandUsage* node) const {
    return show_aliases ? node->joinAliasesInline_(" | ") : node->primary();
}

bool CommandUsage::sameAliases_(const CommandUsage* a, const CommandUsage* b) {
    if (!a || !b) return false;
    return a->aliases == b->aliases && a->pluralAliasImpliesList == b->pluralAliasImpliesList;
}

std::string CommandUsage::join_(const std::vector<std::string>& v, std::string_view sep) {
    std::ostringstream ss;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) ss << sep;
        ss << v[i];
    }
    return ss.str();
}

void CommandUsage::appendWrapped_(std::ostringstream& out,
                               const std::string& text,
                               const size_t width,
                               const size_t indentAfterFirst) {
    if (width == 0) { out << text; return; }
    size_t col = 0;
    bool firstLine = true;
    for (size_t i = 0; i < text.size(); ) {
        if (col == 0 && !firstLine && indentAfterFirst) {
            out << std::string(indentAfterFirst, ' ');
            col += indentAfterFirst;
        }
        const size_t nextSpace = text.find(' ', i);
        std::string_view word = (nextSpace == std::string::npos)
            ? std::string_view(text).substr(i)
            : std::string_view(text).substr(i, nextSpace - i);
        if (const size_t need = word.size() + (col ? 1 : 0); col + need > width && col > 0) {
            out << "\n";
            col = 0;
            firstLine = false;
            continue;
        }
        if (col) { out << ' '; ++col; }
        out << word;
        col += word.size();
        if (nextSpace == std::string::npos) break;
        i = nextSpace + 1;
    }
}

// New synopsis that understands Options/Optional/Flags/Positionals
std::string CommandUsage::buildSynopsis_() const {
    if (synopsis) return *synopsis;

    std::ostringstream syn;
    syn << binName_;

    const auto chain = lineage_();
    constexpr size_t startIdx = 1;  // Skip root (vh) at index 0

    for (size_t i = startIdx; i < chain.size(); ++i) {
        if (i > startIdx && sameAliases_(chain[i], chain[i - 1])) continue;
        syn << ' ' << tokenFor_(chain[i]);
    }

    // Positionals
    for (const auto& p : positionals) {
        // Prefer canonical label for synopsis
        syn << ' ' << fmt::format("<{}>", p.label);
    }

    // Required flags
    for (const auto& f : required_flags) {
        const std::vector<std::string> toks = f.aliases.empty()
            ? std::vector<std::string>{f.label}
            : f.aliases;
        syn << ' ' << synopsisTokenList(toks, show_aliases);
    }

    // Required options
    for (const auto& o : required) {
        const std::vector<std::string> toks = o.option_tokens.empty()
            ? std::vector<std::string>{o.label}
            : o.option_tokens;
        syn << ' ' << synopsisTokenList(toks, show_aliases);
        if (!o.value_tokens.empty()) {
            syn << ' ' << angleChoice(o.value_tokens);
        } else {
            // If no explicit value token names, show a generic placeholder
            syn << " <value>";
        }
    }

    // Optional flags
    for (const auto& f : optional_flags) {
        const std::vector<std::string> toks = f.aliases.empty()
            ? std::vector<std::string>{f.label}
            : f.aliases;
        syn << ' ' << '[' << synopsisTokenList(toks, show_aliases) << ']';
    }

    // Optional options
    for (const auto& o : optional) {
        syn << ' ' << '[';
        const std::vector<std::string> toks = o.option_tokens.empty()
            ? std::vector<std::string>{o.label}
            : o.option_tokens;
        syn << synopsisTokenList(toks, show_aliases);
        if (!o.value_tokens.empty())
            syn << ' ' << angleChoice(o.value_tokens);
        else
            syn << " <value>";
        syn << ']';
    }

    return syn.str();
}

// Emit the command header + description + usage section
void CommandUsage::emitCommand(std::ostringstream& out,
                               const std::shared_ptr<CommandUsage>& command,
                               const bool spaceLines) const {
    const auto cmd = command ? command : shared_from_this();
    const int tw = term_width > 40 ? term_width : 100;

    {
        std::ostringstream head;
        head << cmd->constructCmdString();
        out << theme.C() << head.str() << theme.R() << "\n";
        if (spaceLines) out << "\n";

        out << theme.A2() << "Description:" << theme.R();
        const bool needsSpace = cmd->description.contains("\n");
        if (needsSpace) out << "\n";
        if (cmd->description.empty()) out << "  No description provided.\n";
        else emitWrapped(out, cmd->description, needsSpace ? 2 : 1, tw);
    }

    if (spaceLines) out << "\n";
    out << theme.H() << "Usage:" << theme.R();
    {
        const auto syn = cmd->buildSynopsis_();
        const bool needsSpace = syn.contains("\n");
        if (needsSpace) out << "\n";
        emitWrapped(out, syn, needsSpace ? 2 : 1, tw);
    }
    out << "\n";
}

// ======================================================
// CommandUsage public API
// ======================================================

std::string CommandUsage::str() const {
    const int tw = term_width > 40 ? term_width : 100;
    constexpr std::size_t indent = 2;
    constexpr std::size_t gap = 2;

    std::ostringstream out;

    const auto emitOne = [&](const std::shared_ptr<CommandUsage>& command = nullptr) {
        const auto cmd = command ? command : shared_from_this();

        emitCommand(out, command, true);

        // Sections in a sensible order
        emitTwoColSection(out, "Positionals:", cmd->positionals, indent, gap, tw, max_key_col, theme,
            [&](const Positional& p){ return renderPositional(p, show_aliases); });

        emitTwoColSection(out, "Required Flags:", cmd->required_flags, indent, gap, tw, max_key_col, theme,
            [&](const Flag& f){ return renderFlag(f, show_aliases); });

        emitTwoColSection(out, "Required Options:", cmd->required, indent, gap, tw, max_key_col, theme,
            [&](const Option& o){ return renderOption(o, show_aliases); });

        emitTwoColSection(out, "Optional Flags:", cmd->optional_flags, indent, gap, tw, max_key_col, theme,
            [&](const Flag& f){ return renderFlag(f, show_aliases); });

        emitTwoColSection(out, "Optional Options:", cmd->optional, indent, gap, tw, max_key_col, theme,
            [&](const Optional& o){ return renderOptional(o, show_aliases); });

        for (const auto& g : cmd->groups) {
            emitTwoColSection(out, g.title + ":", g.items, indent, gap, tw, max_key_col, theme,
                [&](const std::variant<Optional,Flag>& v){ return renderGrouped(v, show_aliases); });
        }

        if (!examples.empty()) {
            out << theme.A3() << "Examples:" << theme.R() << "\n";
            for (const auto& ex : cmd->examples) {
                emitWrapped(out, fmt::format("$ {}", ex.cmd), indent, tw);
                if (!ex.note.empty()) emitWrapped(out, ex.note, indent + 2, tw);
                out << "\n";
            }
        }
    };

    emitOne();
    out << "\n" << lineBreak(tw) << "\n\n";
    for (unsigned int i = 0; i < subcommands.size(); ++i) {
        emitOne(subcommands[i]);
        if (i + 1 < subcommands.size()) out << "\n" << lineBreak(tw) << "\n\n";
    }

    return out.str();
}

std::string CommandUsage::basicStr() const {
    if (aliases.empty()) throw std::runtime_error("CommandUsage::basicStr() called with no command");

    std::ostringstream out;
    out << "\n";
    emitCommand(out);
    for (const auto& c : subcommands) emitCommand(out, c);
    return out.str();
}

std::string CommandUsage::markdown() const {
    std::ostringstream md;

    if (aliases.empty()) throw std::runtime_error("CommandUsage::markdown() called with no command");

    // Title
    {
        std::ostringstream head;
        head << constructCmdString();
        md << fmt::format("# `{}`\n\n", head.str());
    }

    if (!description.empty()) md << description << "\n\n";

    if (show_aliases) {
        if (!aliases.empty()) {
            md << "**Command aliases:** ";
            for (std::size_t i = 0; i < aliases.size(); ++i) {
                if (i) md << ", ";
                md << "`" << aliases[i] << "`";
            }
            md << "\n\n";
        }
    }

    // Synopsis
    md << "## Usage\n\n";
    md << "```bash\n" << buildSynopsis_() << "\n```\n\n";

    // Sections
    if (!positionals.empty()) {
        md << "## Positionals\n\n";
        emitMarkdownTable(md, positionals, [&](const Positional& p){ return renderPositional(p, show_aliases); });
        md << "\n";
    }
    if (!required_flags.empty()) {
        md << "## Required Flags\n\n";
        emitMarkdownTable(md, required_flags, [&](const Flag& f){ return renderFlag(f, show_aliases); });
        md << "\n";
    }
    if (!required.empty()) {
        md << "## Required Options\n\n";
        emitMarkdownTable(md, required, [&](const Option& o){ return renderOption(o, show_aliases); });
        md << "\n";
    }
    if (!optional_flags.empty()) {
        md << "## Optional Flags\n\n";
        emitMarkdownTable(md, optional_flags, [&](const Flag& f){ return renderFlag(f, show_aliases); });
        md << "\n";
    }
    if (!optional.empty()) {
        md << "## Optional Options\n\n";
        emitMarkdownTable(md, optional, [&](const Optional& o){ return renderOptional(o, show_aliases); });
        md << "\n";
    }
    for (const auto& g : groups) {
        if (!g.items.empty()) {
            md << "## " << g.title << "\n\n";
            emitMarkdownTable(md, g.items, [&](const std::variant<Optional,Flag>& v){ return renderGrouped(v, show_aliases); });
            md << "\n";
        }
    }

    if (!examples.empty()) {
        md << "## Examples\n\n";
        for (const auto& ex : examples) {
            md << "```bash\n" << ex.cmd << "\n```\n";
            if (!ex.note.empty()) md << ex.note << "\n\n";
        }
    }

    return md.str();
}

std::shared_ptr<CommandUsage> CommandUsage::findSubcommand(const std::string& alias) const {
    const auto it = std::ranges::find_if(
        subcommands,
        [&](const std::shared_ptr<CommandUsage>& c) {
            return std::ranges::any_of(c->aliases, [&](const std::string& a) { return a == alias; });
        }
    );

    if (it != subcommands.end()) return *it;
    return nullptr;
}

std::shared_ptr<Positional> CommandUsage::resolvePositional(const std::string& alias) const {
    const auto it = std::ranges::find_if(
        positionals,
        [&](const Positional& p) {
            return p.label == alias || std::ranges::any_of(p.aliases,
                [&](const std::string& a) { return a == alias; });
        }
    );

    if (it != positionals.end()) return std::make_shared<Positional>(*it);
    return nullptr;
}

std::shared_ptr<Flag> CommandUsage::resolveFlag(const std::string& alias) const {
    const auto it = std::ranges::find_if(
        optional_flags,
        [&](const Flag& f) {
            return f.label == alias || std::ranges::any_of(f.aliases,
                [&](const std::string& a) { return a == alias; });
        }
    );
    if (it != optional_flags.end()) return std::make_shared<Flag>(*it);

    const auto it2 = std::ranges::find_if(
        required_flags,
        [&](const Flag& f) {
            return f.label == alias || std::ranges::any_of(f.aliases,
                [&](const std::string& a) { return a == alias; });
        }
    );

    if (it2 != required_flags.end()) return std::make_shared<Flag>(*it2);
    return nullptr;
}

std::shared_ptr<Optional> CommandUsage::resolveOptional(const std::string& alias) const {
    const auto it = std::ranges::find_if(
        optional,
        [&](const Optional& o) {
            return o.label == alias || std::ranges::any_of(o.option_tokens,
                [&](const std::string& t) { return t == alias; });
        }
    );

    if (it != optional.end()) return std::make_shared<Optional>(*it);
    return nullptr;
}

std::shared_ptr<Option> CommandUsage::resolveRequired(const std::string& alias) const {
    const auto it = std::ranges::find_if(
        required,
        [&](const Option& o) {
            return o.label == alias || std::ranges::any_of(o.option_tokens,
                [&](const std::string& t) { return t == alias; });
        }
    );

    if (it != required.end()) return std::make_shared<Option>(*it);
    return nullptr;
}

std::shared_ptr<GroupedOptions> CommandUsage::resolveGroup(const std::string& alias) const {
    const auto it = std::ranges::find_if(
        groups,
        [&](const GroupedOptions& g) { return g.title == alias; }
    );

    if (it != groups.end()) return std::make_shared<GroupedOptions>(*it);
    return nullptr;
}

std::shared_ptr<Optional> CommandUsage::resolveGroupOptional(const std::string& groupAlias, const std::string& optionalAlias) const {
    const auto group = resolveGroup(groupAlias);
    if (!group) return nullptr;

    const auto it = std::ranges::find_if(
        group->items,
        [&](const std::variant<Optional, Flag>& v) {
            return std::holds_alternative<Optional>(v) &&
                   (std::get<Optional>(v).label == optionalAlias ||
                    std::ranges::any_of(std::get<Optional>(v).option_tokens,
                        [&](const std::string& t) { return t == optionalAlias; }));
        }
    );

    if (it != group->items.end() && std::holds_alternative<Optional>(*it)) {
        return std::make_shared<Optional>(std::get<Optional>(*it));
    }
    return nullptr;
}

}
