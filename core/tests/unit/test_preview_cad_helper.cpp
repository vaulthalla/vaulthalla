// The real vaulthalla-preview-cad helper, driven through preview::derive::Runner. Skipped when the helper was not
// built (meson option preview_cad, or OCCT/libseccomp missing).

#include "preview/derive/Runner.hpp"
#include "storage/PlaintextReader.hpp"

#include <gtest/gtest.h>

#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace vh::preview::derive::test_cad_helper {

namespace fs = std::filesystem;

class BytesReader final : public storage::PlaintextReader {
public:
    explicit BytesReader(std::vector<uint8_t> data) : data_(std::move(data)) { generation_.size = data_.size(); }
    [[nodiscard]] uint64_t size() const override { return data_.size(); }
    std::size_t read(const uint64_t offset, std::span<uint8_t> out) override {
        if (offset >= data_.size()) return 0;
        const auto n = std::min<std::size_t>(out.size(), data_.size() - offset);
        std::memcpy(out.data(), data_.data() + offset, n);
        return n;
    }
    [[nodiscard]] const storage::Generation& generation() const override { return generation_; }

private:
    std::vector<uint8_t> data_;
    storage::Generation generation_;
};

std::vector<uint8_t> bytesOf(const std::string& text) { return {text.begin(), text.end()}; }

std::vector<uint8_t> fixture() {
    std::ifstream in(fs::path(VH_TEST_ASSETS_DIR) / "preview-cad" / "box-and-cylinder.step", std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

const std::string kHeader = "ISO-10303-21;\nHEADER;\nFILE_DESCRIPTION((''),'2;1');\n"
                            "FILE_NAME('x','2026-01-01T00:00:00',(''),(''),'','','');\n"
                            "FILE_SCHEMA(('AUTOMOTIVE_DESIGN { 1 0 10303 214 1 1 1 1 }'));\nENDSEC;\nDATA;\n";
const std::string kFooter = "ENDSEC;\nEND-ISO-10303-21;\n";

class PreviewCadHelperTest : public ::testing::Test {
protected:
    void SetUp() override {
        helper_ = VH_TEST_CAD_HELPER;
        if (helper_.empty() || !Runner::isExecutable(helper_)) GTEST_SKIP() << "vaulthalla-preview-cad not built";
    }

    RunResult convert(std::vector<uint8_t> input, std::vector<std::string> args = {}) {
        BytesReader reader(std::move(input));
        RunRequest r;
        r.executable = helper_;
        r.command = "convert-step";
        r.args = std::move(args);
        r.input = &reader;
        r.limits.wallTimeout = std::chrono::seconds(120);
        r.limits.maxOutputBytes = maxOutput_;
        output_.clear();
        r.sink = [this](std::span<const uint8_t> b) { output_.insert(output_.end(), b.begin(), b.end()); };
        return Runner::run(r);
    }

    fs::path helper_;
    std::vector<uint8_t> output_;
    uint64_t maxOutput_ = 64ull << 20;
};

TEST_F(PreviewCadHelperTest, ConvertsAStepAssemblyToGlb) {
    const auto res = convert(fixture(), {"--max-triangles", "2000000"});
    ASSERT_TRUE(res.ok()) << res.failureMessage() << "\nstderr: " << res.stderrTail;
    ASSERT_GE(output_.size(), 12u);
    EXPECT_EQ(std::string(output_.begin(), output_.begin() + 4), "glTF");
    const uint32_t version = output_[4] | (output_[5] << 8) | (output_[6] << 16) | (static_cast<uint32_t>(output_[7]) << 24);
    const uint32_t length = output_[8] | (output_[9] << 8) | (output_[10] << 16) | (static_cast<uint32_t>(output_[11]) << 24);
    EXPECT_EQ(version, 2u);
    EXPECT_EQ(length, output_.size());

    const auto& j = res.result;
    EXPECT_EQ(j["format"], "glb");
    EXPECT_GT(j["triangles"].get<uint64_t>(), 0u);
    EXPECT_GT(j["nodes"].get<uint64_t>(), 0u);
    ASSERT_EQ(j["bbox"].size(), 6u);
    // The fixture: a 10x20x30 box at the origin and an r=5, h=15 cylinder at x=30.
    EXPECT_NEAR(j["bbox"][0].get<double>(), 0.0, 1e-3);
    EXPECT_NEAR(j["bbox"][3].get<double>(), 35.0, 1e-3);
    EXPECT_NEAR(j["bbox"][5].get<double>(), 30.0, 1e-3);
    EXPECT_EQ(j["output_bytes"].get<uint64_t>(), output_.size());
    EXPECT_EQ(res.outputBytes, output_.size());
    ASSERT_TRUE(j.contains("sandbox"));
    EXPECT_TRUE(j["sandbox"]["seccomp"].get<bool>());
    EXPECT_EQ(j["sandbox"]["landlock"].get<bool>(), j["sandbox"]["landlock_abi"].get<int>() > 0)
        << j["sandbox"].dump();
}

TEST_F(PreviewCadHelperTest, TriangleCapIsALimitFailure) {
    const auto res = convert(fixture(), {"--max-triangles", "10"});
    EXPECT_FALSE(res.ok());
    EXPECT_EQ(res.exitCode, 3) << res.stderrTail;
    EXPECT_EQ(res.failureReason(), "limit_exceeded");
    EXPECT_TRUE(output_.empty());
}

TEST_F(PreviewCadHelperTest, OutputCapIsALimitFailure) {
    maxOutput_ = 1000;
    const auto res = convert(fixture());
    EXPECT_FALSE(res.ok());
    EXPECT_EQ(res.failureReason(), "limit_exceeded") << res.failureMessage();
    EXPECT_TRUE(output_.empty());
}

TEST_F(PreviewCadHelperTest, InputCapIsALimitFailure) {
    const auto res = convert(fixture(), {"--max-input-bytes", "1000"});
    EXPECT_EQ(res.failureReason(), "limit_exceeded") << res.failureMessage();
}

TEST_F(PreviewCadHelperTest, MalformedInputFailsBoundedAndNeverCrashesTheDaemon) {
    std::mt19937_64 rng(1234);
    std::vector<uint8_t> garbage(64 * 1024);
    for (auto& b : garbage) b = static_cast<uint8_t>(rng());

    auto truncated = fixture();
    truncated.resize(truncated.size() * 2 / 5);

    std::string manyEntities = kHeader;
    for (int i = 1; i <= 20000; ++i)
        manyEntities += "#" + std::to_string(i) + "=CARTESIAN_POINT('',(0.,0.,0.));\n";
    manyEntities += kFooter;

    const std::string cyclic = kHeader +
        "#1=SHAPE_REPRESENTATION('',(#2),#3);\n#2=AXIS2_PLACEMENT_3D('',#2,#2,#2);\n"
        "#3=GEOMETRIC_REPRESENTATION_CONTEXT(3);\n#4=SHAPE_DEFINITION_REPRESENTATION(#4,#1);\n" + kFooter;

    const std::string headerOnly = kHeader + kFooter;
    const std::string nonsense = kHeader + "#1=NOT_A_STEP_ENTITY(((((((((((((((((;\n#2=;\n" + kFooter;

    struct Case {
        std::string name;
        std::vector<uint8_t> bytes;
        std::vector<std::string> args;
        std::set<std::string> allowed;
    };
    const std::vector<Case> cases{
        {"empty", {}, {}, {"invalid_input"}},
        {"garbage", garbage, {}, {"invalid_input"}},
        {"truncated", truncated, {}, {"invalid_input", "limit_exceeded"}},
        {"many-entities", bytesOf(manyEntities), {"--max-entities", "1000"}, {"limit_exceeded"}},
        {"cyclic", bytesOf(cyclic), {}, {"invalid_input", "limit_exceeded"}},
        {"header-only", bytesOf(headerOnly), {}, {"invalid_input"}},
        {"nonsense", bytesOf(nonsense), {}, {"invalid_input"}},
    };
    for (const auto& c : cases) {
        const auto res = convert(c.bytes, c.args);
        EXPECT_FALSE(res.ok()) << c.name;
        EXPECT_TRUE(c.allowed.contains(res.failureReason()))
            << c.name << ": " << res.failureMessage() << "\nstderr: " << res.stderrTail;
        EXPECT_FALSE(res.timedOut) << c.name;
    }

    // Still healthy afterwards.
    EXPECT_TRUE(convert(fixture()).ok());
    errno = 0;
    EXPECT_EQ(::waitpid(-1, nullptr, WNOHANG), -1);
    EXPECT_EQ(errno, ECHILD);
}

TEST_F(PreviewCadHelperTest, SandboxForbidsWritesSocketsProcessesAndSignals) {
    const auto dir = fs::temp_directory_path() / ("vh-cad-sandbox-" + std::to_string(::getpid()));
    fs::create_directories(dir);
    const auto secret = dir / "secret";
    std::ofstream(secret) << "plaintext that a converter must not read";
    const auto target = dir / "written-by-helper";

    BytesReader empty({});
    RunRequest r;
    r.executable = helper_;
    r.command = "selftest-sandbox";
    r.args = {"--write-path", target.string(), "--read-path", secret.string()};
    r.input = &empty;
    r.limits.wallTimeout = std::chrono::seconds(30);
    const auto res = Runner::run(r);
    ASSERT_TRUE(res.ok()) << res.failureMessage() << "\nstderr: " << res.stderrTail;

    const auto& t = res.result["selftest"];
    EXPECT_TRUE(t["open_write"]["blocked"].get<bool>()) << t.dump();
    EXPECT_TRUE(t["socket_inet"]["blocked"].get<bool>()) << t.dump();
    EXPECT_TRUE(t["socket_unix"]["blocked"].get<bool>()) << t.dump();
    EXPECT_TRUE(t["fork"]["blocked"].get<bool>()) << t.dump();
    EXPECT_TRUE(t["execve"]["blocked"].get<bool>()) << t.dump();
    EXPECT_TRUE(t["signal_parent"]["blocked"].get<bool>()) << t.dump();
    EXPECT_FALSE(t["open_read_usr"]["blocked"].get<bool>()) << t.dump();
    if (res.result["sandbox"]["landlock"].get<bool>()) {
        EXPECT_TRUE(t["open_read_outside"]["blocked"].get<bool>()) << t.dump();
    }
    EXPECT_FALSE(fs::exists(target));

    fs::remove_all(dir);
}

TEST_F(PreviewCadHelperTest, UnknownCommandIsUnsupported) {
    BytesReader empty({});
    RunRequest r;
    r.executable = helper_;
    r.command = "convert-iges";
    r.input = &empty;
    const auto res = Runner::run(r);
    EXPECT_EQ(res.exitCode, 4);
    EXPECT_EQ(res.failureReason(), "unsupported");
}

}
