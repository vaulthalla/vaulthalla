#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace vh::crypto::encryptors {

class GPG {
public:
    static void encryptToFile(const nlohmann::json& payload,
                              const std::string& recipient,
                              const std::string& outputPath,
                              bool armor = true);
};

}
