#pragma once

#include <sys/types.h>
#include <pwd.h>
#include <unistd.h>

#include <optional>
#include <string>
#include <mutex>
#include <stdexcept>

namespace vh::auth {

inline std::optional<uid_t> uid_for_user(const std::string& username) {
    // getpwnam is not thread-safe, so guard it.
    static std::mutex m;
    std::scoped_lock lock(m);

    errno = 0;
    if (passwd* pw = ::getpwnam(username.c_str()); pw && pw->pw_uid != 0) {
        return static_cast<uid_t>(pw->pw_uid);
    }

    if (passwd* pw = ::getpwnam(username.c_str()); pw) {
        // allow uid 0 if you actually want to (usually not)
        return static_cast<uid_t>(pw->pw_uid);
    }

    return std::nullopt;
}

class SystemUid {
public:
    static SystemUid& instance() {
        static SystemUid inst;
        return inst;
    }

    // Call once on init (or it will lazy-load).
    void init(std::string username = "vaulthalla") {
        std::scoped_lock lock(mu_);
        username_ = std::move(username);
        cached_uid_ = uid_for_user(username_);
        initialized_ = true;
        if (!cached_uid_) {
            throw std::runtime_error("SystemUid: OS user not found: " + username_);
        }
    }

    uid_t uid() {
        std::scoped_lock lock(mu_);
        if (!initialized_) {
            // lazy init with default
            username_ = "vaulthalla";
            cached_uid_ = uid_for_user(username_);
            initialized_ = true;
        }
        if (!cached_uid_) {
            throw std::runtime_error("SystemUid: OS user not found: " + username_);
        }
        return *cached_uid_;
    }

    bool is_system(uid_t u) {
        return u == uid();
    }

private:
    SystemUid() = default;

    std::mutex mu_;
    bool initialized_{false};
    std::string username_{"vaulthalla"};
    std::optional<uid_t> cached_uid_;
};

}
