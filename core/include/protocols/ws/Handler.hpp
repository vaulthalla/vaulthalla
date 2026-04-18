#pragma once

#include <memory>

namespace vh::protocols::ws {

class Router;

class Handler {
  public:
    static void registerAllHandlers(const std::shared_ptr<Router>& r);

  private:
    static void registerAuthHandlers(const std::shared_ptr<Router>& r);
    static void registerFileSystemHandlers(const std::shared_ptr<Router>& r);
    static void registerStorageHandlers(const std::shared_ptr<Router>& r);
    static void registerAPIKeyHandlers(const std::shared_ptr<Router>& r);
    static void registerRoleHandlers(const std::shared_ptr<Router>& r);
    static void registerPermissionsHandlers(const std::shared_ptr<Router>& r);
    static void registerSettingsHandlers(const std::shared_ptr<Router>& r);
    static void registerGroupHandlers(const std::shared_ptr<Router>& r);
    static void registerStatHandlers(const std::shared_ptr<Router>& r);
};

}
