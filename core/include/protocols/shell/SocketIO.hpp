#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>
#include <string_view>

namespace vh::protocols::shell {

struct IO {
    virtual ~IO() = default;
    virtual void print(std::string_view s) = 0;
    virtual bool confirm(std::string_view prompt, bool def_no) = 0;
    virtual std::string prompt(std::string_view prompt,
                               std::string_view def) = 0;
};

class SocketIO final : public IO {
public:
    explicit SocketIO(int fd);
    void print(std::string_view msg) override;
    bool confirm(std::string_view promptIn, bool def_no) override;
    bool confirm(std::string_view promptIn);
    std::string prompt(std::string_view promptIn, std::string_view def) override;
    std::string prompt(std::string_view promptIn);

    static nlohmann::json recv_json(int fd);
    static void send_json(int fd, const nlohmann::json& j);

private:
    int fd_, id_counter_ = 0;
    std::string next_id();
};

}
