#include "fuse/Builder.hpp"
#include "cmd/generators.hpp"
#include "types/Type.hpp"
#include "fuse/helpers.hpp"

#include "identities/User.hpp"
#include "identities/Group.hpp"

#include "vault/model/Vault.hpp"

#include "db/query/rbac/role/Admin.hpp"
#include "db/query/rbac/role/Vault.hpp"
#include "db/query/rbac/role/vault/Assignments.hpp"
#include "db/query/identities/User.hpp"
#include "db/query/identities/Group.hpp"
#include "db/query/rbac/Permission.hpp"

#include "sync/model/LocalPolicy.hpp"
#include "runtime/Deps.hpp"

#include "storage/Engine.hpp"
#include "storage/Manager.hpp"
#include "fs/model/Path.hpp"

#include "rbac/role/Vault.hpp"

using namespace vh;
using namespace vh::test::integration::fuse;
using namespace vh::identities;

static unsigned int id_index = 1001;

Builder Builder::make(BuilderSpec&& spec) {
    const auto admin = db::query::identities::User::getUserByName("admin");
    if (!admin || !admin->meta.linux_uid) throw std::runtime_error("Admin user/uid not found");

    const auto engine = makeVault();
    if (!engine) throw std::runtime_error("Failed to create vault");

    const auto root = std::filesystem::path(engine->paths->fuseRoot / fs::model::to_snake_case(engine->vault->name));
    if (!admin->meta.linux_uid) throw std::runtime_error("Admin Linux UID not found");
    seed_vault_tree(*admin->meta.linux_uid, root, spec.baseDir);

    return Builder{FuseScenarioContext{ "FUSE: " + spec.name, admin, engine, root, spec.baseDir }};
}

std::shared_ptr<storage::Engine> Builder::makeVault() {
    auto vault = std::make_shared<vault::model::Vault>();
    vault->name = generateVaultName("vault/create");
    vault->description = "Test Vault";
    vault->owner_id = db::query::identities::User::getUserByName("admin")->id;

    if (vault->name.empty()) throw std::runtime_error("Vault name cannot be empty");

    const auto sync = std::make_shared<sync::model::LocalPolicy>();
    sync->interval = std::chrono::minutes(15);
    sync->conflict_policy = sync::model::LocalPolicy::ConflictPolicy::Overwrite;

    vault = runtime::Deps::get().storageManager->addVault(vault, sync);
    return runtime::Deps::get().storageManager->getEngine(vault->id);
}

void Builder::seed_vault_tree(const uid_t admin_uid, const std::filesystem::path &root, const std::string &base) {
    (void)mkdir_as(admin_uid, root / base / "docs");
    (void)write_as(admin_uid, root / base / "docs" / "secret.txt", "TOP SECRET\n");
    (void)write_as(admin_uid, root / base / "note.txt", "hello\n");
}

void Builder::buildAssignVRole(VaultRoleSpec&& spec) {
    const auto role = db::query::rbac::role::Vault::get(spec.templateName);
    if (!role) throw std::runtime_error("Builder::buildVaultRole(): template role not found: " + spec.templateName);

    role->id = 0;
    role->name = spec.roleNameSeed;
    role->description = spec.description;

    db::query::rbac::role::Vault::upsert(role);

    role->fs.overrides = spec.overrides;

    switch (spec.subjectType) {
        case TargetSubject::User:
            role->assign(subject_.user->id, spec.getSubjectType(), ctx_.engine->vault->id);
            subject_.userVaultRole = role;
            subject_.user->roles.vaults[ctx_.engine->vault->id] = role;
            break;
        case TargetSubject::Group:
            role->assign(subject_.group->id, spec.getSubjectType(), ctx_.engine->vault->id);
            subject_.groupVaultRole = role;
            subject_.group->roles.vaults[ctx_.engine->vault->id] = role;
            break;
        default:
            throw std::runtime_error("Builder::buildVaultRole(): invalid subject kind");
    }

    db::query::rbac::role::vault::Assignments::assign(ctx_.engine->vault->id, spec.getSubjectType(), role->assignment->subject_id, role->id);
}

void Builder::makeUser(UserSpec&& userSpec) {
    const auto user = std::make_shared<User>();
    user->name = generateName(userSpec.userNameSeed);

    user->roles.admin = db::query::rbac::role::Admin::get(userSpec.adminRoleTemplateName);
    if (!user->roles.admin) throw std::runtime_error("Failed to create admin role: " + userSpec.adminRoleTemplateName);

    user->meta.linux_uid = id_index++;
    subject_.uid = *user->meta.linux_uid;
    uids_.push_back(*user->meta.linux_uid);

    bool nameException = false;

    do {
        try {
            if (nameException) {
                user->name = generateName("user/create");
                nameException = false;
            }
            user->id = db::query::identities::User::createUser(user);
        } catch (const std::exception& e) {
            if (std::string(e.what()).contains("Key (name)=() already exists")) nameException = true;
            else throw std::runtime_error("Failed to create user: " + std::string(e.what()));
        }
    } while (nameException);

    subject_.user = user;
}

void Builder::makeGroup(std::string&& nameSeed) {
    const auto group = std::make_shared<Group>();
    group->name = generateName(nameSeed);

    group->linux_gid = id_index++;
    gids_.push_back(*group->linux_gid);
    subject_.gid = *group->linux_gid;

    bool nameException = false;

    do {
        try {
            if (nameException) {
                group->name = generateName("group/create");
                nameException = false;
            }
            group->id = db::query::identities::Group::createGroup(group);
        } catch (const std::exception& e) {
            if (std::string(e.what()).contains("Key (name)=() already exists")) nameException = true;
            else throw std::runtime_error("Failed to create group: " + std::string(e.what()));
        }
    } while (nameException);

    subject_.group = group;
}

void Builder::makeOverride(OverrideSpec &&spec) const {
    const auto perm = db::query::rbac::Permission::getPermissionByName(spec.permName);
    if (!perm) throw std::runtime_error("Permission not found: " + spec.permName);

    rbac::permission::Override ov;
    ov.permission = *perm;
    ov.assignment_id = spec.subjectType == TargetSubject::User ?
        subject_.userVaultRole->assignment_id : subject_.groupVaultRole->assignment_id;
    ov.effect = spec.effect;
    ov.enabled = spec.enabled;
    ov.pattern.source = spec.pattern;

    if (spec.subjectType == TargetSubject::User) subject_.userVaultRole->fs.overrides.push_back(ov);
    else subject_.groupVaultRole->fs.overrides.push_back(ov);
}

void Builder::makeTestCase(FuseTestCaseSpec &&spec) {
    auto tc = std::make_shared<TestCase>();
    tc->name = std::move(spec.name);
    tc->path = std::move(spec.path);         // e.g. "fuse/mkdir"
    tc->expect_exit = spec.expect_exit;      // 0 or EACCES, etc.
    tc->must_contain = std::move(spec.must_contain);
    tc->must_not_contain = std::move(spec.must_not_contain);
    ctx_.steps.emplace_back(std::move(tc), std::move(spec.fn));
}

test::integration::TestStage Builder::exec() const {
    return {
        .name = ctx_.name,
        .tests = runSteps(),
        .uids = uids_,
        .gids = gids_
    };
}

std::vector<std::shared_ptr<test::integration::TestCase>> Builder::runSteps() const {
    std::vector<std::shared_ptr<TestCase>> out;
    out.reserve(ctx_.steps.size());

    for (const auto& s : ctx_.steps) {
        if (!s.tc || !s.fn) continue;
        const ExecResult exec = s.fn();

        // Map into TestCase::result
        s.tc->result.exit_code  = exec.exit_code;
        s.tc->result.stdout_text = exec.stdout_text;
        s.tc->result.stderr_text.clear(); // FUSE ops print only to stdout in our helpers

        // Pre-evaluate expectation (validateStage can still re-check if you prefer)
        bool ok = (s.tc->expect_exit == exec.exit_code);
        if (ok && !s.tc->must_contain.empty()) {
            for (const auto& needle : s.tc->must_contain) {
                if (s.tc->result.stdout_text.find(needle) == std::string::npos) { ok = false; break; }
            }
        }
        if (ok && !s.tc->must_not_contain.empty()) {
            for (const auto& bad : s.tc->must_not_contain) {
                if (s.tc->result.stdout_text.find(bad) != std::string::npos) { ok = false; break; }
            }
        }
        s.tc->assertion = ok ? AssertionResult::Pass()
                             : AssertionResult::Fail("FUSE: expectation mismatch (exit/stdout)");

        out.push_back(s.tc);
    }
    return out;
}

void Builder::addUserToGroup() {
    if (!subject_.user || !subject_.group) throw std::runtime_error("Builder::addUserToGroup(): subject user/group not set");
    db::query::identities::Group::addMemberToGroup(subject_.group->id, subject_.user->id);
    subject_.group = db::query::identities::Group::getGroup(subject_.group->id);
}
