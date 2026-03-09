#pragma once

#include "session/Manager.hpp"

#include <memory>
#include <string>
#include <unordered_map>

namespace vh::identities::model { struct User; }
namespace vh::protocols::ws { class Session; }
namespace vh::storage { class Manager; }

namespace vh::auth {

namespace model { struct RefreshToken; }
namespace session { class Manager; }

class Manager {
public:
    void registerUser(std::shared_ptr<identities::model::User> user,
                                         const std::string& password);

    void loginUser(const std::string& name, const std::string& password,
                                      const std::shared_ptr<protocols::ws::Session>& session);

    void updateUser(const std::shared_ptr<identities::model::User>& user);

    void changePassword(const std::string& name, const std::string& oldPassword, const std::string& newPassword);

    std::shared_ptr<identities::model::User> findUser(const std::string& name);

private:
    std::unordered_map<std::string, std::shared_ptr<identities::model::User>> users_;
};

}
