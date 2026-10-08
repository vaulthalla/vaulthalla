#include "preview/Plan.hpp"

#include "fs/model/File.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>

namespace vh::preview {

namespace {

[[nodiscard]] std::string lower(std::string value) {
    std::ranges::transform(value, value.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

[[nodiscard]] std::string extensionOf(const std::string_view name) {
    return lower(std::filesystem::path(std::string(name)).extension().string());
}

template<std::size_t N>
[[nodiscard]] bool oneOf(const std::string& value, const std::array<std::string_view, N>& set) {
    return std::ranges::find(set, value) != set.end();
}

// Decodable by the server render path (stb_image / TurboJPEG).
constexpr std::array<std::string_view, 8> kRenderedImageMimes{
    "image/jpeg", "image/pjpeg", "image/png", "image/gif", "image/bmp", "image/x-ms-bmp",
    "image/x-portable-anymap", "image/x-portable-pixmap"};

// Displayed by browsers from the original bytes (never re-encoded by the server).
constexpr std::array<std::string_view, 3> kNativeImageMimes{"image/webp", "image/avif", "image/x-icon"};

constexpr std::array<std::string_view, 5> kMarkdownExts{".md", ".markdown", ".mdown", ".mkd", ".mdx"};

constexpr std::array<std::string_view, 72> kTextExts{
    ".txt", ".text", ".log", ".csv", ".tsv", ".json", ".jsonc", ".json5", ".ndjson", ".yaml", ".yml", ".toml",
    ".ini", ".conf", ".cfg", ".env", ".properties", ".xml", ".html", ".htm", ".css", ".scss", ".sass", ".less",
    ".js", ".mjs", ".cjs", ".ts", ".tsx", ".jsx", ".vue", ".svelte", ".c", ".h", ".cc", ".cpp", ".cxx", ".hpp",
    ".hh", ".hxx", ".rs", ".go", ".py", ".rb", ".php", ".java", ".kt", ".kts", ".scala", ".swift", ".cs", ".m",
    ".mm", ".lua", ".pl", ".r", ".jl", ".dart", ".sh", ".bash", ".zsh", ".fish", ".ps1", ".sql", ".gradle",
    ".cmake", ".mk", ".dockerfile", ".tex", ".rst", ".adoc", ".srt"};

constexpr std::array<std::string_view, 8> kTextMimes{
    "application/json", "application/xml", "application/x-yaml", "application/yaml", "application/toml",
    "application/javascript", "application/x-sh", "application/sql"};

constexpr std::array<std::string_view, 3> kStepExts{".step", ".stp", ".p21"};

[[nodiscard]] PreviewPlan plan(const PreviewKind kind, std::string renderer, const Capability capability,
                               const bool thumbnail = false, std::vector<std::string> derived = {}) {
    return PreviewPlan{
        .kind = kind,
        .renderer = std::move(renderer),
        .capability = capability,
        .thumbnail = thumbnail,
        .derived = std::move(derived)
    };
}

}

PreviewPlan classify(const fs::model::File& file) {
    return classify(file.name.empty() ? file.path.filename().string() : file.name, file.mime_type);
}

PreviewPlan classify(const std::string_view name, const std::optional<std::string>& mimeType) {
    const auto ext = extensionOf(name);
    auto mime = lower(mimeType.value_or(""));
    if (const auto semi = mime.find(';'); semi != std::string::npos) mime.resize(semi);

    // 3D first: libmagic labels most model files text/plain or application/octet-stream.
    if (ext == ".glb" || mime == "model/gltf-binary") return plan(PreviewKind::ClientModel, "model:glb", Capability::Download);
    if (ext == ".gltf" || mime == "model/gltf+json") return plan(PreviewKind::ClientModel, "model:gltf", Capability::Download);
    if (ext == ".stl" || mime == "model/stl" || mime == "application/sla")
        return plan(PreviewKind::ClientModel, "model:stl", Capability::Download);
    if (ext == ".obj" && (mime.empty() || mime.starts_with("text/") || mime == "model/obj" ||
                          mime == "application/octet-stream"))
        return plan(PreviewKind::ClientModel, "model:obj", Capability::Download);
    if (oneOf(ext, kStepExts) || mime == "model/step" || mime == "application/step")
        return plan(PreviewKind::DerivedArtifact, "derived:step-glb", Capability::Download, false, {"model-glb"});

    if (mime == "image/svg+xml" || ext == ".svg") return plan(PreviewKind::NativeMedia, "svg", Capability::Download);
    if (oneOf(mime, kNativeImageMimes)) return plan(PreviewKind::NativeMedia, "image-native", Capability::Download);
    if (oneOf(mime, kRenderedImageMimes)) return plan(PreviewKind::RenderedImage, "image", Capability::Preview, true);
    if (mime == "application/pdf") return plan(PreviewKind::RenderedImage, "pdf", Capability::Preview, true);

    if (mime.starts_with("video/"))
        return plan(PreviewKind::NativeMedia, "video", Capability::Download, false,
                    {"poster-jpg", "probe-json", "transcode-h264-720", "transcode-h264-1080", "transcode-h264-480"});
    if (mime.starts_with("audio/"))
        return plan(PreviewKind::NativeMedia, "audio", Capability::Download, false, {"probe-json", "transcode-h264-480"});

    if (oneOf(ext, kMarkdownExts) && (mime.empty() || mime.starts_with("text/") || mime == "application/octet-stream"))
        return plan(PreviewKind::TextDocument, "markdown", Capability::Download);
    if (mime.starts_with("text/") || oneOf(mime, kTextMimes) ||
        (oneOf(ext, kTextExts) && (mime.empty() || mime == "application/octet-stream" || mime == "inode/x-empty")))
        return plan(PreviewKind::TextDocument, "text", Capability::Download);

    return {};
}

std::string_view to_string(const PreviewKind kind) {
    switch (kind) {
        case PreviewKind::RenderedImage: return "rendered_image";
        case PreviewKind::NativeMedia: return "native_media";
        case PreviewKind::ClientModel: return "client_model";
        case PreviewKind::TextDocument: return "text_document";
        case PreviewKind::DerivedArtifact: return "derived_artifact";
        case PreviewKind::Unsupported: return "unsupported";
    }
    return "unsupported";
}

std::string_view to_string(const Capability capability) {
    return capability == Capability::Download ? "download" : "preview";
}

std::optional<Capability> derivedKindCapability(const std::string_view kind) {
    if (kind == "poster-jpg" || kind == "thumbnail" || kind == "render") return Capability::Preview;
    if (kind == "model-glb" || kind == "probe-json" || kind.starts_with("transcode-") || kind.starts_with("hls-"))
        return Capability::Download;
    return std::nullopt;
}

void to_json(nlohmann::json& j, const PreviewPlan& p) {
    j = nlohmann::json{
        {"kind", to_string(p.kind)},
        {"renderer", p.renderer},
        {"requires", to_string(p.capability)},
        {"thumbnail", p.thumbnail}
    };
    if (!p.derived.empty()) j["derived"] = p.derived;
}

}
