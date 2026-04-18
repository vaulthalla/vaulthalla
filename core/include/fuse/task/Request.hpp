#pragma once

#include "concurrency/Task.hpp"
#include <fuse3/fuse_lowlevel.h>

namespace vh::fuse::task {

class Request final : public concurrency::Task {
public:
    Request(fuse_session* session, const fuse_buf& buf) : session_(session), buf_(buf) {}

    void operator()() override {
        try {
            fuse_session_process_buf(session_, &buf_);
        } catch (const std::exception& ex) {
            log::Registry::fuse()->critical("[fuse::task::Request] Unhandled exception: {}", ex.what());
        } catch (...) {
            log::Registry::fuse()->critical("[fuse::task::Request] Unhandled unknown exception");
        }
    }

private:
    fuse_session* session_;
    fuse_buf buf_;
};

}
