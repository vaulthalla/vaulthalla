#pragma once

#define FUSE_USE_VERSION 35

#include "resolver/Resolved.hpp"

#include <fuse_lowlevel.h>
#include <sys/stat.h>
#include <string_view>

namespace vh::fuse {
    struct Reply {
        static int err(fuse_req_t req, int errnum);

        static bool failIfNotOk(
            fuse_req_t req,
            std::string_view caller,
            const resolver::Resolved& resolved
        );

        static int entry(fuse_req_t req, const fuse_entry_param& entry);
        static int attr(fuse_req_t req, const struct stat& stbuf, double timeout);
        static int open(fuse_req_t req, fuse_file_info& fi);
        static int buf(fuse_req_t req, const char* data, size_t size);
        static int write(fuse_req_t req, size_t bytes);
        static int none(fuse_req_t req);

        static int invalid(fuse_req_t req);
        static int access(fuse_req_t req);
        static int noent(fuse_req_t req);
        static int io(fuse_req_t req);
    };
}
