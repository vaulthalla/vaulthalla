# pragma once

#include <memory>
#include <string>
#include <nlohmann/json_fwd.hpp>

namespace pqxx {
    class row;
}

namespace vh::fs::model {
    struct File;
}

namespace vh::sync::model {

struct Artifact {
    enum class Side { LOCAL, UPSTREAM };

    uint32_t id{}, conflict_id{};
    std::shared_ptr<fs::model::File> file{};
    Side side{};

    Artifact() = default;
    explicit Artifact(const pqxx::row& row);
    explicit Artifact(const std::shared_ptr<fs::model::File>& f, const Side& s);

    [[nodiscard]] std::string sideToString() const;
    void parseSide(const std::string& sideStr);
};

void to_json(nlohmann::json& j, const Artifact& artifact);

}
