#pragma once

#include "fs/Fwd.hpp"

#include <cstdint>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vh::preview {

// How a file can be consumed in the console. Server-authoritative: the web renders whatever plan the server
// attaches to the file JSON instead of re-deriving it from MIME types.
enum class PreviewKind {
    RenderedImage,    // server renders JPEG (images, PDF pages); thumbnails
    NativeMedia,      // the browser consumes the original bytes (video, audio, SVG, WebP)
    ClientModel,      // the browser renders the original 3D bytes (GLB, glTF, STL, OBJ)
    TextDocument,     // UTF-8 text the console can show and (with Overwrite) edit
    DerivedArtifact,  // the browser needs a server-derived artifact (STEP -> GLB)
    Unsupported
};

// What the viewer must be allowed to do. Preview = lossy server renders only. Download = original bytes or a
// full-fidelity derivative of them.
enum class Capability { Preview, Download };

struct PreviewPlan {
    PreviewKind kind{PreviewKind::Unsupported};
    std::string renderer{"none"};
    Capability capability{Capability::Preview};
    bool thumbnail{false};
    std::vector<std::string> derived;  // derived artifact kinds this file can have (first = primary)
};

[[nodiscard]] PreviewPlan classify(const fs::model::File& file);
[[nodiscard]] PreviewPlan classify(std::string_view name, const std::optional<std::string>& mimeType);

[[nodiscard]] std::string_view to_string(PreviewKind kind);
[[nodiscard]] std::string_view to_string(Capability capability);

// Derived artifact kinds and the capability each requires.
[[nodiscard]] std::optional<Capability> derivedKindCapability(std::string_view kind);

void to_json(nlohmann::json& j, const PreviewPlan& plan);

}
