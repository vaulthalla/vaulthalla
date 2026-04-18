#pragma once

#include <ctime>
#include <optional>
#include <string>
#include <pqxx/row>

namespace vh::fs::model::file {

struct Share {
    unsigned int id;
    unsigned int file_acl_id;
    unsigned int shared_by;
    std::string share_token;
    std::optional<std::time_t> expires_at;
    std::time_t created_at;

    explicit Share(const pqxx::row& row);
};

}
