#pragma once

// Forward declarations for the shell protocol and its usage model (the usage types are defined in core/usage).
// Declarations only: include the defining header to use a type.

namespace vh::protocols::shell {
    class Router;
    class CommandUsage;
    class UsageManager;
    struct CommandCall;
}
