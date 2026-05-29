#include "storage/s3/provider/Registry.hpp"

#include "storage/s3/provider/Aws.hpp"
#include "storage/s3/provider/CloudflareR2.hpp"
#include "storage/s3/provider/Generic.hpp"

namespace vh::storage::s3::provider {

ProfilePtr resolve(const vault::model::S3Provider provider) {
    using vault::model::S3Provider;

    static const auto aws = std::make_shared<AwsProfile>();
    static const auto r2 = std::make_shared<CloudflareR2Profile>();
    static const auto wasabi = std::make_shared<GenericProfile>("Wasabi");
    static const auto backblaze = std::make_shared<GenericProfile>("Backblaze B2");
    static const auto digitalOcean = std::make_shared<GenericProfile>("DigitalOcean");
    static const auto minio = std::make_shared<GenericProfile>("MinIO");
    static const auto ceph = std::make_shared<GenericProfile>("Ceph");
    static const auto storj = std::make_shared<GenericProfile>("Storj");
    static const auto other = std::make_shared<GenericProfile>("Other");

    switch (provider) {
    case S3Provider::AWS:
        return aws;
    case S3Provider::CloudflareR2:
        return r2;
    case S3Provider::Wasabi:
        return wasabi;
    case S3Provider::BackblazeB2:
        return backblaze;
    case S3Provider::DigitalOcean:
        return digitalOcean;
    case S3Provider::MinIO:
        return minio;
    case S3Provider::Ceph:
        return ceph;
    case S3Provider::Storj:
        return storj;
    case S3Provider::Other:
        return other;
    }

    return other;
}

TierResolution normalizeStorageTier(
    const vault::model::S3Provider provider,
    const std::optional<std::string>& requested) {
    return resolve(provider)->normalizeStorageTier(requested);
}

} // namespace vh::storage::s3::provider
