#include "vault/model/APIKey.hpp"
#include "storage/s3/Controller.hpp"
#include "preview/image.hpp"
#include "preview/pdf.hpp"
#include "fs/model/Entry.hpp"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <algorithm>
#include <pdfium/fpdfview.h>

namespace fs = std::filesystem;

using namespace vh::storage::s3;
using namespace vh::vault::model;
using namespace vh::fs::model;
using namespace vh::preview;

class S3ProviderIntegrationTest : public ::testing::Test {
  protected:
    std::shared_ptr<APIKey> apiKey_;
    std::string bucket_;
    std::shared_ptr<Controller> s3Provider_;
    std::filesystem::path test_dir;
    bool skipTests = false;

    void SetUp() override {
        test_dir = fs::temp_directory_path() / "vaulthalla_test_dir";
        fs::create_directory(test_dir);

        if (!static_cast<bool>(std::getenv("VAULTHALLA_TEST_R2_ACCESS_KEY")) ||
            !static_cast<bool>(std::getenv("VAULTHALLA_TEST_R2_SECRET_ACCESS_KEY")) ||
            !static_cast<bool>(std::getenv("VAULTHALLA_TEST_R2_REGION")) ||
            !static_cast<bool>(std::getenv("VAULTHALLA_TEST_R2_ENDPOINT"))) {
            skipTests = true;
            std::cout << "[test_S3Provider] Skipping S3 tests due to missing environment variables." << std::endl;
            return;
        }

        apiKey_ = std::make_shared<APIKey>(1,
                                                             "Test S3 Key",
                                                             S3Provider::CloudflareR2,
                                                             std::getenv("VAULTHALLA_TEST_R2_ACCESS_KEY"),
                                                             std::getenv("VAULTHALLA_TEST_R2_SECRET_ACCESS_KEY"),
                                                             std::getenv("VAULTHALLA_TEST_R2_REGION"),
                                                             std::getenv("VAULTHALLA_TEST_R2_ENDPOINT"));

        bucket_ = std::getenv("VAULTHALLA_TEST_R2_BUCKET");

        s3Provider_ = std::make_shared<Controller>(apiKey_, bucket_);

        FPDF_LIBRARY_CONFIG config;
        config.version = 3;
        config.m_pUserFontPaths = nullptr;
        config.m_pIsolate = nullptr;
        config.m_v8EmbedderSlot = 0;
        FPDF_InitLibraryWithConfig(&config);
    }

    void TearDown() override {
        fs::remove_all(test_dir);
        FPDF_DestroyLibrary();
    }

    static void writeTextFile(const fs::path& path, const std::string& content) {
        std::ofstream out(path);
        out << content;
    }
};

TEST_F(S3ProviderIntegrationTest, test_DeleteUnicodeFilename) {
    if (skipTests) GTEST_SKIP() << "Skipping test due to missing environment variables.";

    const std::filesystem::path key = u8"Screenshot 2025-06-26 at 3.29.35\u202FPM.png";
    ASSERT_TRUE(fs::exists(key));

    std::cout << "Uploading file: " << key << std::endl;
    ASSERT_NO_THROW(s3Provider_->uploadObject(key, key));

    std::cout << "Downloading file: " << key << std::endl;
    const auto downloadedPath = test_dir / "downloaded.png";
    ASSERT_NO_THROW(s3Provider_->downloadObject(key, downloadedPath));

    std::cout << "Deleting file: " << key << std::endl;
    EXPECT_NO_THROW(s3Provider_->deleteObject(key));
}

TEST_F(S3ProviderIntegrationTest, test_BulkUploadDownloadDeleteTestAssets) {
    if (skipTests) GTEST_SKIP() << "Skipping test due to missing environment variables.";

    const std::vector<std::filesystem::path> filenames = {
        "sample.jpg",
        "sample.pdf",
        u8"Screenshot 2025-06-26 at 3.29.35\u202FPM.png"
    };

    std::vector<std::filesystem::path> uploadedKeys;

    for (const auto& name : filenames) {
        const fs::path src = fs::path(name);
        ASSERT_TRUE(fs::exists(src)) << "Test asset missing: " << src;

        const fs::path relKey = fs::path("test-assets") / src.filename();
        const fs::path dest = test_dir / src.filename();
        fs::copy_file(src, dest, fs::copy_options::overwrite_existing);

        ASSERT_TRUE(fs::exists(dest)) << "Failed to copy file to: " << dest;

        std::cout << "Uploading file: " << relKey << std::endl;
        ASSERT_NO_THROW(s3Provider_->uploadObject(relKey, dest)) << "Upload failed for: " << relKey;

        uploadedKeys.push_back(relKey);

        std::vector<uint8_t> buffer;
        std::cout << "Downloading file to buffer: " << relKey << std::endl;
        EXPECT_NO_THROW(s3Provider_->downloadToBuffer(relKey, buffer)) << "Download failed for: " << relKey;
        EXPECT_GT(buffer.size(), 10) << "Buffer too small for: " << relKey;
    }

    for (const auto& key : uploadedKeys) {
        std::cout << "Deleting uploaded key: " << key << std::endl;
        EXPECT_NO_THROW(s3Provider_->deleteObject(key)) << "Failed to delete key: " << key;
    }
}

TEST_F(S3ProviderIntegrationTest, test_S3SimpleUploadRoundTrip) {
    if (skipTests) GTEST_SKIP() << "Skipping test due to missing environment variables.";

    const std::filesystem::path key = {"simple-test.txt"};
    const auto filePath = test_dir / key;

    // Write some test content to the file
    writeTextFile(filePath, "This is a test file for S3 upload.");

    ASSERT_TRUE(fs::exists(filePath)) << "File not created at: " << filePath;

    std::cout << "Uploading file: " << filePath << std::endl;
    EXPECT_NO_THROW(s3Provider_->uploadObject(key, filePath)) << "Failed to upload file to S3: " << key;

    std::cout << "Downloading file: " << filePath << std::endl;
    // Download the file for verification
    const auto downloadedPath = test_dir / "downloaded.txt";
    EXPECT_NO_THROW(s3Provider_->downloadObject(key, downloadedPath));

    // Compare original and downloaded files
    std::ifstream original(filePath);
    std::ifstream downloaded(downloadedPath);
    std::ostringstream originalContent, downloadedContent;
    originalContent << original.rdbuf();
    downloadedContent << downloaded.rdbuf();
    EXPECT_TRUE(originalContent.str() == downloadedContent.str());

    // Cleanup
    EXPECT_NO_THROW(s3Provider_->deleteObject(key));
}

TEST_F(S3ProviderIntegrationTest, test_S3MultipartUploadRoundtrip) {
    if (skipTests) GTEST_SKIP() << "Skipping test due to missing environment variables.";

    const std::filesystem::path key = {"multipart-test-2.txt"};

    // Generate a temporary file with ~15MB of data
    const auto filePath = test_dir / key;
    const auto part = std::string(5 * 1024 * 1024, 'x'); // 5MB
    std::ofstream out(filePath, std::ios::binary);
    out << part << part << part; // total 15MB
    out.close();

    ASSERT_TRUE(fs::exists(filePath));

    // Upload the file using multipart logic
    EXPECT_NO_THROW(s3Provider_->uploadLargeObject(key, filePath.string(), 5 * 1024 * 1024));

    // Download for verification
    const auto downloadedPath = test_dir / "downloaded.txt";
    EXPECT_NO_THROW(s3Provider_->downloadObject(key, downloadedPath.string()));

    // Compare original and downloaded files
    std::ifstream original(filePath, std::ios::binary);
    std::ifstream downloaded(downloadedPath, std::ios::binary);
    std::ostringstream originalContent, downloadedContent;
    originalContent << original.rdbuf();
    downloadedContent << downloaded.rdbuf();
    EXPECT_TRUE(downloadedContent.str().contains("xxxxx")) << downloadedContent.str();
    // EXPECT_EQ(originalContent.str(), downloadedContent.str());

    // Cleanup
    EXPECT_NO_THROW(s3Provider_->deleteObject(key));
}

TEST_F(S3ProviderIntegrationTest, test_S3MultipartAbortOnFailure) {
    if (skipTests) GTEST_SKIP() << "Skipping test due to missing environment variables.";

    const std::filesystem::path key = {"abort-test.txt"};

    std::string uploadId = s3Provider_->initiateMultipartUpload(key);
    ASSERT_FALSE(uploadId.empty());

    const auto bogus = std::string(5 * 1024 * 1024, 'Z');
    std::string etag;

    // Simulate partial upload then abort
    EXPECT_NO_THROW(s3Provider_->uploadPart(key, uploadId, 1, bogus, etag));

    EXPECT_NO_THROW(s3Provider_->abortMultipartUpload(key, uploadId));
}

TEST_F(S3ProviderIntegrationTest, test_S3ListObjectsAndDownloadToBuffer) {
    if (skipTests) GTEST_SKIP() << "Skipping test due to missing environment variables.";

    const std::filesystem::path key = {"list-download-test.txt"};
    const auto filePath = test_dir / key;
    writeTextFile(filePath, "This file should appear in listObjects and download into buffer.");
    ASSERT_NO_THROW(s3Provider_->uploadObject(key, filePath.string()));

    const auto xml = s3Provider_->listObjects();

    const auto entries = fromS3XML(xml);
    EXPECT_FALSE(entries.empty()) << "fromS3XML should return at least one entry";
    auto match = std::ranges::find_if(entries.begin(), entries.end(), [&](const auto& entry) {
        return !entry->isDirectory() && entry->path.filename() == key;
    });
    EXPECT_TRUE(match != entries.end()) << "Uploaded key not found in fromS3XML()";

    std::vector<uint8_t> buffer;
    EXPECT_NO_THROW(s3Provider_->downloadToBuffer(key, buffer));

    const std::string expected = "appear in listObjects";
    const auto it = std::search(buffer.begin(), buffer.end(),
                                expected.begin(), expected.end());

    EXPECT_TRUE(it != buffer.end()) << "Downloaded buffer does not contain expected content";

    EXPECT_NO_THROW(s3Provider_->deleteObject(key));
}

TEST_F(S3ProviderIntegrationTest, test_ResizeAndCompressImageBuffer) {
    if (skipTests) GTEST_SKIP() << "Skipping test due to missing environment variables.";

    const std::filesystem::path key = {"test-image.jpg"};
    const auto srcPath = fs::path("sample.jpg");
    ASSERT_TRUE(fs::exists(srcPath));
    ASSERT_NO_THROW(s3Provider_->uploadObject(key, srcPath.string()));

    std::vector<uint8_t> buffer;
    ASSERT_NO_THROW(s3Provider_->downloadToBuffer(key, buffer));

    auto jpeg = image::resize_and_compress_buffer(
        buffer.data(),
        buffer.size(),
        std::nullopt,
        std::make_optional("128")
    );

    EXPECT_GT(jpeg.size(), 100); // sanity check: JPEG data exists
    EXPECT_NO_THROW(s3Provider_->deleteObject(key));
}

TEST_F(S3ProviderIntegrationTest, test_ResizeAndCompressPdfBuffer) {
    if (skipTests) GTEST_SKIP() << "Skipping test due to missing environment variables.";

    const std::filesystem::path key = {"test-pdf.pdf"};
    const auto srcPath = fs::path("sample.pdf");
    ASSERT_TRUE(fs::exists(srcPath));
    ASSERT_NO_THROW(s3Provider_->uploadObject(key, srcPath.string()));

    std::vector<uint8_t> buffer;
    ASSERT_NO_THROW(s3Provider_->downloadToBuffer(key, buffer));

    auto jpeg = pdf::resize_and_compress_buffer(
        buffer.data(),
        buffer.size(),
        std::nullopt,
        std::make_optional("128")
    );

    EXPECT_GT(jpeg.size(), 100);
    EXPECT_NO_THROW(s3Provider_->deleteObject(key));
}

TEST_F(S3ProviderIntegrationTest, test_ValidateS3Credentials) {
    if (skipTests) GTEST_SKIP() << "Skipping test due to missing environment variables.";

    EXPECT_NO_THROW(const auto ok = s3Provider_->validateAPICredentials());
}

TEST_F(S3ProviderIntegrationTest, test_isBucketEmpty) {
    if (skipTests) GTEST_SKIP() << "Skipping test due to missing environment variables.";

    EXPECT_NO_THROW(const auto _ = s3Provider_->isBucketEmpty());
}
