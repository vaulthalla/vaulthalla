#pragma once

#include <memory>

namespace vh::identities { struct User; }
namespace vh::share { struct Principal; }

namespace vh::rbac {

class Actor {
public:
    enum class Kind { Human, Share };

    static Actor human(std::shared_ptr<identities::User> user);
    static Actor share(std::shared_ptr<vh::share::Principal> principal);

    [[nodiscard]] Kind kind() const noexcept { return kind_; }
    [[nodiscard]] bool isHuman() const noexcept { return kind_ == Kind::Human; }
    [[nodiscard]] bool isShare() const noexcept { return kind_ == Kind::Share; }
    [[nodiscard]] bool canUseHumanPrivileges() const noexcept { return isHuman(); }

    [[nodiscard]] std::shared_ptr<identities::User> user() const noexcept { return user_; }
    [[nodiscard]] std::shared_ptr<vh::share::Principal> sharePrincipal() const noexcept { return principal_; }

private:
    explicit Actor(Kind kind) : kind_(kind) {}

    Kind kind_{Kind::Human};
    std::shared_ptr<identities::User> user_;
    std::shared_ptr<vh::share::Principal> principal_;
};

}
