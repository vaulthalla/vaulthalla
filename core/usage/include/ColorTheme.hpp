#pragma once

#include <string>

namespace vh::protocols::shell {

struct ColorTheme {
    bool enabled = true;

    // === Primary roles ===
    std::string header     = "\033[1;36m";  // bright cyan — section titles
    std::string command    = "\033[1;32m";  // bright green — commands
    std::string key        = "\033[33m";    // yellow — keys/field labels
    std::string value      = "\033[0;37m";  // light gray — values

    // === UI styling ===
    std::string dim        = "\033[2m";     // dim gray — muted/secondary
    std::string bold       = "\033[1m";     // bold text
    std::string italic     = "\033[3m";     // italic if supported
    std::string underline  = "\033[4m";     // underline if supported

    // === Status colors ===
    std::string success    = "\033[1;32m";  // bright green — success
    std::string warning    = "\033[1;33m";  // bright yellow — caution
    std::string error      = "\033[1;31m";  // bright red — errors
    std::string info       = "\033[1;34m";  // bright blue — info/debug

    // === Accents ===
    std::string accent1    = "\033[35m";    // magenta — flair
    std::string accent2    = "\033[95m";    // bright magenta — flair bold
    std::string accent3    = "\033[38;5;208m"; // orange (256-color)

    // === Reset
    std::string reset      = "\033[0m";

    // === Helper accessors ===
    [[nodiscard]] std::string maybe(const std::string& code) const { return enabled ? code : ""; }

    // Shortcodes
    [[nodiscard]] std::string H() const { return maybe(header); }
    [[nodiscard]] std::string C() const { return maybe(command); }
    [[nodiscard]] std::string K() const { return maybe(key); }
    [[nodiscard]] std::string V() const { return maybe(value); }
    [[nodiscard]] std::string D() const { return maybe(dim); }
    [[nodiscard]] std::string B() const { return maybe(bold); }
    [[nodiscard]] std::string I() const { return maybe(italic); }
    [[nodiscard]] std::string U() const { return maybe(underline); }

    [[nodiscard]] std::string OK() const   { return maybe(success); }
    [[nodiscard]] std::string WARN() const { return maybe(warning); }
    [[nodiscard]] std::string ERR() const  { return maybe(error); }
    [[nodiscard]] std::string INFO() const { return maybe(info); }

    [[nodiscard]] std::string A1() const { return maybe(accent1); }
    [[nodiscard]] std::string A2() const { return maybe(accent2); }
    [[nodiscard]] std::string A3() const { return maybe(accent3); }

    [[nodiscard]] std::string R() const { return maybe(reset); }
};

}
