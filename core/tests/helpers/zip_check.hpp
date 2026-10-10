#pragma once

// Independent validation of ZIP archives produced by the HTTP folder download (#143): Python's zipfile (reads the
// central directory, checks every local header against it and the CRC of every member) and Info-ZIP `unzip -t`.
// Tests skip when the tool is missing.

#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace vh::test::zip {

struct Item {
    std::string name;
    uint64_t size{};
    uint32_t crc{};
    bool directory{};
    uint32_t flags{};
};

struct Listing {
    std::string badMember;  // empty: every CRC matched
    std::vector<Item> items;
};

[[nodiscard]] inline bool haveTool(const std::string& tool) {
    return std::system(("command -v " + tool + " >/dev/null 2>&1").c_str()) == 0;
}

[[nodiscard]] inline std::string runCapture(const std::string& command, int& status) {
    std::string out;
    FILE* pipe = ::popen(command.c_str(), "r");
    if (!pipe) {
        status = -1;
        return out;
    }
    char buf[65536];
    std::size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), pipe)) > 0) out.append(buf, n);
    status = ::pclose(pipe);
    return out;
}

// nullopt when python3 is unavailable. Throws (via nlohmann) when zipfile rejects the archive outright.
[[nodiscard]] inline std::optional<Listing> pythonCheck(const std::filesystem::path& archive) {
    if (!haveTool("python3")) return std::nullopt;
    static const std::string script =
        "import sys, json, zipfile\n"
        "try:\n"
        "    with zipfile.ZipFile(sys.argv[1]) as z:\n"
        "        bad = z.testzip()\n"
        "        items = [{'name': i.filename, 'size': i.file_size, 'crc': i.CRC, 'dir': i.is_dir(),\n"
        "                  'flags': i.flag_bits} for i in z.infolist()]\n"
        "    print(json.dumps({'bad': bad or '', 'items': items}))\n"
        "except Exception as e:\n"
        "    print(json.dumps({'error': repr(e)}))\n";
    const auto scriptPath = archive.parent_path() / "zip_check.py";
    {
        FILE* f = std::fopen(scriptPath.c_str(), "w");
        if (!f) return std::nullopt;
        std::fwrite(script.data(), 1, script.size(), f);
        std::fclose(f);
    }
    int status = 0;
    const auto out = runCapture("python3 -I '" + scriptPath.string() + "' '" + archive.string() + "'", status);
    const auto json = nlohmann::json::parse(out);
    if (json.contains("error")) throw std::runtime_error("zipfile rejected the archive: " + json.at("error").get<std::string>());
    Listing listing;
    listing.badMember = json.at("bad").get<std::string>();
    for (const auto& item : json.at("items"))
        listing.items.push_back({item.at("name").get<std::string>(), item.at("size").get<uint64_t>(),
                                 item.at("crc").get<uint32_t>(), item.at("dir").get<bool>(),
                                 item.at("flags").get<uint32_t>()});
    return listing;
}

// -1 when unzip is unavailable, else its exit status (0: every member tested OK).
[[nodiscard]] inline int unzipTest(const std::filesystem::path& archive) {
    if (!haveTool("unzip")) return -1;
    return std::system(("unzip -tqq '" + archive.string() + "' >/dev/null 2>&1").c_str());
}

}
