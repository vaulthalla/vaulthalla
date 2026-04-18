#pragma once

#include "session/Manager.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <mutex>

namespace vh::identities { struct User; }
namespace vh::protocols::ws { class Session; }
namespace vh::storage { class Manager; }

namespace vh::auth {

namespace model { struct RefreshToken; }
namespace session { class Manager; }

class Manager {
public:
    void registerUser(std::shared_ptr<identities::User> user,
                                         const std::string& password);

    void loginUser(const std::string& name, const std::string& password,
                                      const std::shared_ptr<protocols::ws::Session>& session);

    void updateUser(const std::shared_ptr<identities::User>& user);

    void changePassword(const std::string& name, const std::string& oldPassword, const std::string& newPassword);

    std::shared_ptr<identities::User> getUser(const std::string& name);
    std::shared_ptr<identities::User> getUser(uint32_t id);

    void cacheUser(std::shared_ptr<identities::User> user);
    void evictUser(std::shared_ptr<identities::User> user);

private:
    std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<identities::User>> usersByName_;
    std::unordered_map<uint32_t, std::shared_ptr<identities::User>> usersById_;
};

}
