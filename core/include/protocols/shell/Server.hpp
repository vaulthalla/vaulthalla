#pragma once

#include "concurrency/AsyncService.hpp"

#include <mutex>
#include <memory>
#include <string>

namespace vh::protocols::shell {

class Router;

class Server final : public concurrency::AsyncService {
public:
    Server();
    ~Server() override;

    [[nodiscard]] std::shared_ptr<Router> get_router() const { return router_; }

    void setSocketPath(const std::string& path) { socketPath_ = path; }
    [[nodiscard]] const std::string& socketPath() const noexcept { return socketPath_; }

    [[nodiscard]] bool adminUIDSet() const noexcept { return adminUIDSet_.load(); }

protected:
    void runLoop() override;
    void onStop() override;

private:
    static constexpr std::string_view kAddAdminCmd = "usermod -aG vaulthalla {}";
    static constexpr std::string_view kVerifyInAdminGroup = R"(id -Gn {} | grep -qw vaulthalla)";

    std::shared_ptr<Router> router_;
    std::string socketPath_;
    unsigned adminGid_;
    mutable std::mutex fdMutex_;
    int listenFd_ = -1;
    int activeClientFd_ = -1;
    std::atomic<bool> adminUIDSet_;

    void closeListener();
    void closeActiveClient();
    void closeClient(int fd);
    void initAdminUid(int cfd, uid_t uid);
};

}
