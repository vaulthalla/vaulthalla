#pragma once

#include <string_view>

namespace vh::seed {

static constexpr std::string_view ADMIN_DEFAULT_VAULT_NAME = "Admin Default Vault";

void seed_database();
void initPermissions();
void initRoles();
void initSystemUser();
void initAdmin();
void initAdminGroup();
void initAdminDefaultVault();
void initRoot();
void reconcileSystemPrincipals();

// dev
void cleanupDevR2TestBucket();
void initDevCloudVault();

}
