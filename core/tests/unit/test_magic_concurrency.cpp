#include "fs/metadata/Magic.hpp"

#include <gtest/gtest.h>

#include <unistd.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr char kPngBytes[] = "\x89PNG\r\n\x1a\n\0\0\0\rIHDR\0\0\0\x01\0\0\0\x01\x08\x02\0\0\0";
const std::string kPng(kPngBytes, sizeof(kPngBytes) - 1);
const std::string kPdf = "%PDF-1.7\n1 0 obj\n<< /Type /Catalog >>\nendobj\ntrailer\n<< /Root 1 0 R >>\n%%EOF\n";
const std::string kText = "plain words on a line\nand another line of ordinary text\n";

}

// The static cookies behind Magic::get_mime_type* are shared by FUSE seals, uploads and overwrites on many threads.
// libmagic cookies are not thread-safe; unguarded, this corrupted the heap and crashed the daemon mid-flush (which
// then wedged its own FUSE mount). Every answer must stay exact under contention.
TEST(MagicConcurrency, SharedCookiesStayCorrectAcrossThreads) {
    const auto dir = std::filesystem::temp_directory_path() / ("vh_magic_" + std::to_string(::getpid()));
    std::filesystem::create_directories(dir);
    const auto pngPath = dir / "a.bin";
    const auto pdfPath = dir / "b.bin";
    std::ofstream(pngPath, std::ios::binary) << kPng;
    std::ofstream(pdfPath, std::ios::binary) << kPdf;

    using vh::fs::metadata::Magic;
    const auto expectedPng = Magic::get_mime_type_from_buffer(kPng);
    const auto expectedPdf = Magic::get_mime_type_from_buffer(kPdf);
    const auto expectedText = Magic::get_mime_type_from_buffer(kText);
    ASSERT_EQ(expectedPng, "image/png");
    ASSERT_EQ(expectedPdf, "application/pdf");
    ASSERT_EQ(expectedText, "text/plain");

    std::atomic<int> mismatches{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 8; ++t) {
        threads.emplace_back([&, t] {
            for (int i = 0; i < 400; ++i) {
                switch ((t + i) % 4) {
                    case 0: if (Magic::get_mime_type_from_buffer(kPng) != expectedPng) ++mismatches; break;
                    case 1: if (Magic::get_mime_type_from_buffer(std::vector<uint8_t>(kPdf.begin(), kPdf.end())) != expectedPdf) ++mismatches; break;
                    case 2: if (Magic::get_mime_type(pngPath.string()) != expectedPng) ++mismatches; break;
                    default: if (Magic::get_mime_type(pdfPath.string()) != expectedPdf) ++mismatches; break;
                }
            }
        });
    }
    for (auto& th : threads) th.join();
    std::filesystem::remove_all(dir);
    EXPECT_EQ(mismatches.load(), 0);
}
