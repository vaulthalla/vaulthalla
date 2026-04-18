#pragma once

#include <memory>
#include <string>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <filesystem>

namespace vh::log {

class Registry {
public:
    // Initialize all loggers with sinks/levels.
    static void init();

    // Generic access by name
    static std::shared_ptr<spdlog::logger> get(const std::string& name);

    // Subsystem shorthands
    static std::shared_ptr<spdlog::logger> vaulthalla()  { return get("vaulthalla"); }
    static std::shared_ptr<spdlog::logger> fuse()        { return get("fuse"); }
    static std::shared_ptr<spdlog::logger> fs()          { return get("filesystem"); }
    static std::shared_ptr<spdlog::logger> cloud()       { return get("cloud"); }
    static std::shared_ptr<spdlog::logger> crypto()      { return get("crypto"); }
    static std::shared_ptr<spdlog::logger> sync()        { return get("sync"); }
    static std::shared_ptr<spdlog::logger> thumb()       { return get("thumb"); }
    static std::shared_ptr<spdlog::logger> storage()     { return get("storage"); }
    static std::shared_ptr<spdlog::logger> auth()        { return get("auth"); }
    static std::shared_ptr<spdlog::logger> ws()          { return get("ws"); }
    static std::shared_ptr<spdlog::logger> http()        { return get("http"); }
    static std::shared_ptr<spdlog::logger> shell()       { return get("shell"); }
    static std::shared_ptr<spdlog::logger> db()          { return get("db"); }
    static std::shared_ptr<spdlog::logger> types()       { return get("types"); }
    static std::shared_ptr<spdlog::logger> audit()       { return get("audit"); }
    static std::shared_ptr<spdlog::logger> runtime()     { return get("runtime"); }

    [[nodiscard]] static bool isInitialized();

    static void reopenMainLog();
    static void reopenAuditLog();

private:
    static constexpr const auto* FILE_LOG_FORMAT = "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] %v";
    static constexpr const auto* CONSOLE_LOG_FORMAT = "[%^%l%$] [%n] %v";

    static inline bool initialized_ = false;

    static inline std::filesystem::path log_dir_;
    static inline std::filesystem::path main_log_path_;
    static inline std::filesystem::path audit_log_path_;

    // keep the shared sinks so we can swap them later
    static inline std::shared_ptr<spdlog::sinks::stdout_color_sink_mt> console_sink_;
    static inline std::shared_ptr<spdlog::sinks::rotating_file_sink_mt> main_file_sink_;
    static inline std::shared_ptr<spdlog::sinks::basic_file_sink_mt>    audit_file_sink_;

    // remember main sink params you used in init()
    static inline size_t main_max_bytes_ = 10 * 1024 * 1024; // 10 MiB
    static inline size_t main_max_files_ = 5;

    static void replaceSinkEverywhere_(const std::shared_ptr<spdlog::sinks::sink>& old_sink,
                                       const std::shared_ptr<spdlog::sinks::sink>& new_sink);

};

}
