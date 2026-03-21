#pragma once

#include <cstdint>
#include <string>
#include <optional>
#include <string_view>
#include <nlohmann/json_fwd.hpp>

namespace pqxx {
    class row;
}

namespace vh::rbac::role {
    struct BasicMeta {
        std::time_t created_at{}, updated_at{};
        std::optional<time_t> assigned_at{};

        virtual ~BasicMeta() = default;

        BasicMeta() = default;

        explicit BasicMeta(const pqxx::row &row);

        [[nodiscard]] virtual std::string toString(uint8_t indent) const;

        [[nodiscard]] static std::string bool_to_string(const bool b) { return b ? "true" : "false"; }

        static std::string formatPermissionTable(
            const std::vector<std::string> &flags,
            std::string_view header,
            std::string_view footer
        );
    };

    struct Meta : BasicMeta {
        uint32_t id{}, assignment_id{};
        std::string name, description;

        Meta() = default;

        explicit Meta(const pqxx::row &row);

        explicit Meta(const nlohmann::json &json);

        [[nodiscard]] std::string toString(uint8_t indent) const override;
    };

    void to_json(nlohmann::json &j, const BasicMeta &m);

    void to_json(nlohmann::json &j, const Meta &m);

    void from_json(const nlohmann::json &j, Meta &m);
}
