#include "fuse/Reply.hpp"
#include "log/Registry.hpp"

#include <cerrno>

namespace vh::fuse {
    int Reply::err(fuse_req_t req, const int errnum) {
        fuse_reply_err(req, errnum);
        return 0;
    }

    bool Reply::failIfNotOk(fuse_req_t req,
                            const std::string_view caller,
                            const resolver::Resolved& resolved) {
        if (resolved.ok()) return false;

        log::Registry::fuse()->error(
            "[{}] Resolver failed: status={}, errno={}",
            caller,
            static_cast<int>(resolved.status),
            resolved.errnum
        );

        fuse_reply_err(req, resolved.errnum ? resolved.errnum : EIO);
        return true;
    }

    int Reply::entry(fuse_req_t req, const fuse_entry_param& entry) {
        fuse_reply_entry(req, &entry);
        return 0;
    }

    int Reply::attr(fuse_req_t req, const struct stat& stbuf, const double timeout) {
        fuse_reply_attr(req, &stbuf, timeout);
        return 0;
    }

    int Reply::open(fuse_req_t req, fuse_file_info& fi) {
        fuse_reply_open(req, &fi);
        return 0;
    }

    int Reply::buf(fuse_req_t req, const char* data, const size_t size) {
        fuse_reply_buf(req, data, size);
        return 0;
    }

    int Reply::write(fuse_req_t req, const size_t bytes) {
        fuse_reply_write(req, bytes);
        return 0;
    }

    int Reply::none(fuse_req_t req) {
        fuse_reply_none(req);
        return 0;
    }

    int Reply::invalid(fuse_req_t req) { return err(req, EINVAL); }
    int Reply::access(fuse_req_t req)  { return err(req, EACCES); }
    int Reply::noent(fuse_req_t req)   { return err(req, ENOENT); }
    int Reply::io(fuse_req_t req)      { return err(req, EIO); }
}
