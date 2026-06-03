#include "protocols/s3/Xml.hpp"

#include <cctype>
#include <iomanip>
#include <sstream>

namespace vh::protocols::s3::xml {

namespace {
std::string quotedOrRaw(const std::string& etag) {
    if (!etag.empty() && etag.front() == '"') return etag;
    return "\"" + etag + "\"";
}

bool urlUnreserved(const unsigned char c) {
    return std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~';
}

std::string urlEncode(const std::string_view value) {
    std::ostringstream out;
    out << std::uppercase << std::hex;
    for (const unsigned char c : value) {
        if (urlUnreserved(c)) out << static_cast<char>(c);
        else out << '%' << std::setw(2) << std::setfill('0') << static_cast<unsigned>(c);
    }
    return out.str();
}

std::string maybeEncode(const std::string_view value, const bool encodeUrl) {
    return encodeUrl ? urlEncode(value) : std::string(value);
}
}

std::string escape(const std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (const char c : value) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&apos;"; break;
        default: out.push_back(c); break;
        }
    }
    return out;
}

std::string iso8601(const std::time_t ts) {
    std::ostringstream out;
    out << std::put_time(std::gmtime(&ts), "%Y-%m-%dT%H:%M:%S.000Z");
    return out.str();
}

std::string httpDate(const std::time_t ts) {
    std::ostringstream out;
    out << std::put_time(std::gmtime(&ts), "%a, %d %b %Y %H:%M:%S GMT");
    return out.str();
}

std::string error(
    const std::string& code,
    const std::string& message,
    const std::string& resource,
    const std::string& requestId) {
    std::ostringstream out;
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        << "<Error>"
        << "<Code>" << escape(code) << "</Code>"
        << "<Message>" << escape(message) << "</Message>"
        << "<Resource>" << escape(resource) << "</Resource>"
        << "<RequestId>" << escape(requestId) << "</RequestId>"
        << "</Error>";
    return out.str();
}

std::string listBuckets(const std::vector<Bucket>& buckets, const std::string& ownerName) {
    std::ostringstream out;
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        << "<ListAllMyBucketsResult xmlns=\"http://s3.amazonaws.com/doc/2006-03-01/\">"
        << "<Owner><ID>" << escape(ownerName) << "</ID><DisplayName>" << escape(ownerName) << "</DisplayName></Owner>"
        << "<Buckets>";
    for (const auto& bucket : buckets)
        out << "<Bucket><Name>" << escape(bucket.name) << "</Name><CreationDate>"
            << iso8601(bucket.created_at) << "</CreationDate></Bucket>";
    out << "</Buckets></ListAllMyBucketsResult>";
    return out.str();
}

std::string listObjectsV2(
    const std::string& bucketName,
    const db::query::s3::ObjectListResult& result,
    const std::string& prefix,
    const std::optional<std::string>& delimiter,
    const uint32_t maxKeys,
    const std::optional<std::string>& encodingType) {
    const bool encodeUrl = encodingType == "url";
    std::ostringstream out;
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        << "<ListBucketResult xmlns=\"http://s3.amazonaws.com/doc/2006-03-01/\">"
        << "<Name>" << escape(bucketName) << "</Name>"
        << "<Prefix>" << escape(maybeEncode(prefix, encodeUrl)) << "</Prefix>"
        << "<KeyCount>" << (result.objects.size() + result.common_prefixes.size()) << "</KeyCount>"
        << "<MaxKeys>" << maxKeys << "</MaxKeys>"
        << "<IsTruncated>" << (result.is_truncated ? "true" : "false") << "</IsTruncated>";
    if (encodingType) out << "<EncodingType>" << escape(*encodingType) << "</EncodingType>";
    if (delimiter) out << "<Delimiter>" << escape(maybeEncode(*delimiter, encodeUrl)) << "</Delimiter>";
    if (result.next_continuation_token)
        out << "<NextContinuationToken>" << escape(maybeEncode(*result.next_continuation_token, encodeUrl))
            << "</NextContinuationToken>";
    for (const auto& object : result.objects)
        out << "<Contents>"
            << "<Key>" << escape(maybeEncode(object.object_key, encodeUrl)) << "</Key>"
            << "<LastModified>" << iso8601(object.last_modified) << "</LastModified>"
            << "<ETag>" << escape(quotedOrRaw(object.etag)) << "</ETag>"
            << "<Size>" << object.size_bytes << "</Size>"
            << "<StorageClass>" << escape(object.storage_class.value_or("STANDARD")) << "</StorageClass>"
            << "</Contents>";
    for (const auto& commonPrefix : result.common_prefixes)
        out << "<CommonPrefixes><Prefix>" << escape(maybeEncode(commonPrefix, encodeUrl)) << "</Prefix></CommonPrefixes>";
    out << "</ListBucketResult>";
    return out.str();
}

std::string deleteResult(
    const std::vector<DeletedObject>& deleted,
    const std::vector<DeleteError>& errors,
    const bool quiet) {
    std::ostringstream out;
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?><DeleteResult xmlns=\"http://s3.amazonaws.com/doc/2006-03-01/\">";
    if (!quiet) {
        for (const auto& item : deleted)
            out << "<Deleted><Key>" << escape(item.key) << "</Key></Deleted>";
    }
    for (const auto& err : errors)
        out << "<Error><Key>" << escape(err.key) << "</Key><Code>" << escape(err.code)
            << "</Code><Message>" << escape(err.message) << "</Message></Error>";
    out << "</DeleteResult>";
    return out.str();
}

std::string initiateMultipartUpload(const std::string& bucket, const std::string& key, const std::string& uploadId) {
    std::ostringstream out;
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        << "<InitiateMultipartUploadResult xmlns=\"http://s3.amazonaws.com/doc/2006-03-01/\">"
        << "<Bucket>" << escape(bucket) << "</Bucket><Key>" << escape(key) << "</Key>"
        << "<UploadId>" << escape(uploadId) << "</UploadId></InitiateMultipartUploadResult>";
    return out.str();
}

std::string completeMultipartUpload(
    const std::string& location,
    const std::string& bucket,
    const std::string& key,
    const std::string& etag) {
    std::ostringstream out;
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        << "<CompleteMultipartUploadResult xmlns=\"http://s3.amazonaws.com/doc/2006-03-01/\">"
        << "<Location>" << escape(location) << "</Location>"
        << "<Bucket>" << escape(bucket) << "</Bucket>"
        << "<Key>" << escape(key) << "</Key>"
        << "<ETag>" << escape(quotedOrRaw(etag)) << "</ETag>"
        << "</CompleteMultipartUploadResult>";
    return out.str();
}

std::string listMultipartUploads(const std::string& bucket, const std::vector<db::query::s3::MultipartUpload>& uploads) {
    std::ostringstream out;
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        << "<ListMultipartUploadsResult xmlns=\"http://s3.amazonaws.com/doc/2006-03-01/\">"
        << "<Bucket>" << escape(bucket) << "</Bucket>";
    for (const auto& upload : uploads)
        out << "<Upload><Key>" << escape(upload.object_key) << "</Key><UploadId>" << escape(upload.upload_id)
            << "</UploadId><Initiated>" << iso8601(upload.initiated_at) << "</Initiated></Upload>";
    out << "</ListMultipartUploadsResult>";
    return out.str();
}

std::string listParts(
    const std::string& bucket,
    const std::string& key,
    const std::string& uploadId,
    const std::vector<db::query::s3::MultipartPart>& parts) {
    std::ostringstream out;
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        << "<ListPartsResult xmlns=\"http://s3.amazonaws.com/doc/2006-03-01/\">"
        << "<Bucket>" << escape(bucket) << "</Bucket><Key>" << escape(key) << "</Key>"
        << "<UploadId>" << escape(uploadId) << "</UploadId>";
    for (const auto& part : parts)
        out << "<Part><PartNumber>" << part.part_number << "</PartNumber><LastModified>"
            << iso8601(part.created_at) << "</LastModified><ETag>" << escape(quotedOrRaw(part.etag))
            << "</ETag><Size>" << part.size_bytes << "</Size></Part>";
    out << "</ListPartsResult>";
    return out.str();
}

} // namespace vh::protocols::s3::xml
