#pragma once

#include <filesystem>
#include <optional>
#include <unordered_map>
#include <memory>

namespace vh::storage { struct Engine; }
namespace vh::fs::model { struct File; }

namespace vh::protocols::http::model::preview {

struct Request {
    unsigned int vault_id;
    std::filesystem::path rel_path;
    std::optional<unsigned int> size;
    std::optional<float> scale;
    std::shared_ptr<storage::Engine> engine{nullptr};
    std::shared_ptr<fs::model::File> file{nullptr};

    explicit Request(const std::unordered_map<std::string, std::string>& params) {
        if (!params.contains("vault_id") || !params.contains("path"))
            throw std::invalid_argument("Missing vault_id or path");

        vault_id = std::stoul(params.at("vault_id"));
        rel_path = std::filesystem::path(params.at("path"));

        if (params.contains("size"))
            if (const unsigned int s = std::stoul(params.at("size")); s > 0) size = s;

        if (params.contains("scale"))
            if (const float s = std::stof(params.at("scale")); s > 0) scale = s;
    }

    [[nodiscard]] std::optional<std::string> sizeStr() const {
        if (!size) return std::nullopt;
        return std::to_string(size.value());
    }

    [[nodiscard]] std::optional<std::string> scaleStr() const {
        if (!scale) return std::nullopt;
        return std::to_string(scale.value());
    }
};


}
