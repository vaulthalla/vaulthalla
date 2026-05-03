#pragma once

#include <ctime>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pqxx { class row; }

namespace vh::share {

struct EmailChallenge {
    std::string id;
    std::string share_id;
    std::optional<std::string> share_session_id;
    std::vector<uint8_t> email_hash;
    std::vector<uint8_t> code_hash;
    uint32_t attempts{};
    uint32_t max_attempts{6};
    std::time_t expires_at{};
    std::optional<std::time_t> consumed_at;
    std::time_t created_at{};
    std::optional<std::string> ip_address;
    std::optional<std::string> user_agent;

    EmailChallenge() = default;
    explicit EmailChallenge(const pqxx::row& row);

    static std::string normalizeEmail(std::string_view email);
    static std::vector<uint8_t> hashEmail(std::string_view normalized_email);
    static std::string generateCode();
    static std::vector<uint8_t> hashCode(std::string_view code);
    static bool verifyCode(const std::vector<uint8_t>& expected_hash, std::string_view code);

    [[nodiscard]] bool isExpired(std::time_t now) const;
    [[nodiscard]] bool canAttempt(std::time_t now) const;
};

}
