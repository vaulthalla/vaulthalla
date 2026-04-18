#pragma once

namespace vh::test::integration::cli {

struct Config {
    unsigned int numUsers = 10,
           numVaults = 15,
           numGroups = 5,
           numAdminRoles = 7,
           numVaultRoles = 7;

    static Config Default() { return Config{}; }

    static Config Minimal() {
        return Config{
            .numUsers = 2,
            .numVaults = 2,
            .numGroups = 1,
            .numAdminRoles = 1,
            .numVaultRoles = 1
        };
    }

    static Config Medium() {
        return Config{
            .numUsers = 15,
            .numVaults = 20,
            .numGroups = 10,
            .numAdminRoles = 10,
            .numVaultRoles = 10
        };
    }

    static Config Large() {
        return Config{
            .numUsers = 50,
            .numVaults = 75,
            .numGroups = 20,
            .numAdminRoles = 15,
            .numVaultRoles = 15
        };
    }

    static Config ExtraLarge() {
        return Config{
            .numUsers = 100,
            .numVaults = 150,
            .numGroups = 50,
            .numAdminRoles = 25,
            .numVaultRoles = 25
        };
    }

    static Config NG_STRESS() {
        return Config{
            .numUsers = 500,
            .numVaults = 750,
            .numGroups = 100,
            .numAdminRoles = 50,
            .numVaultRoles = 50
        };
    }
};

}
