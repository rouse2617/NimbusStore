// S3 XML 响应生成器 (参考 Ceph Formatter)
#pragma once

#include "nebulastore/protocol/s3_types.h"
#include <sstream>

namespace nebulastore {
namespace s3 {

constexpr const char* XMLNS_AWS_S3 = "http://s3.amazonaws.com/doc/2006-03-01/";

class S3XMLFormatter {
public:
    static std::string ListBucketsResult(const std::string& owner_id, const std::string& owner_name,
                                         const std::vector<BucketInfo>& buckets) {
        std::ostringstream xml;
        xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        xml << "<ListAllMyBucketsResult xmlns=\"" << XMLNS_AWS_S3 << "\">\n";
        xml << "  <Owner><ID>" << Escape(owner_id) << "</ID><DisplayName>" << Escape(owner_name) << "</DisplayName></Owner>\n";
        xml << "  <Buckets>\n";
        for (const auto& b : buckets)
            xml << "    <Bucket><Name>" << Escape(b.name) << "</Name><CreationDate>" << b.creation_date << "</CreationDate></Bucket>\n";
        xml << "  </Buckets>\n</ListAllMyBucketsResult>";
        return xml.str();
    }

    static std::string ListBucketResult(const ListObjectsResult& r) {
        std::ostringstream xml;
        xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        xml << "<ListBucketResult xmlns=\"" << XMLNS_AWS_S3 << "\">\n";
        xml << "  <Name>" << Escape(r.bucket_name) << "</Name>\n";
        xml << "  <Prefix>" << Escape(r.prefix) << "</Prefix>\n";
        xml << "  <Marker>" << Escape(r.marker) << "</Marker>\n";
        xml << "  <MaxKeys>" << r.max_keys << "</MaxKeys>\n";
        xml << "  <IsTruncated>" << (r.is_truncated ? "true" : "false") << "</IsTruncated>\n";
        for (const auto& obj : r.objects) {
            xml << "  <Contents>\n";
            xml << "    <Key>" << Escape(obj.key) << "</Key>\n";
            xml << "    <LastModified>" << obj.last_modified << "</LastModified>\n";
            xml << "    <ETag>\"" << obj.etag << "\"</ETag>\n";
            xml << "    <Size>" << obj.size << "</Size>\n";
            xml << "    <StorageClass>" << obj.storage_class << "</StorageClass>\n";
            xml << "  </Contents>\n";
        }
        for (const auto& p : r.common_prefixes)
            xml << "  <CommonPrefixes><Prefix>" << Escape(p) << "</Prefix></CommonPrefixes>\n";
        xml << "</ListBucketResult>";
        return xml.str();
    }

private:
    static std::string Escape(const std::string& s) {
        std::string r;
        for (char c : s) {
            switch (c) {
                case '&': r += "&amp;"; break;
                case '<': r += "&lt;"; break;
                case '>': r += "&gt;"; break;
                case '"': r += "&quot;"; break;
                default: r += c;
            }
        }
        return r;
    }
};

} // namespace s3
} // namespace nebulastore
