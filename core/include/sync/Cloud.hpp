#pragma once

#include "Local.hpp"
#include "sync/tasks/Delete.hpp"
#include "model/helpers.hpp"

#include <memory>
#include <unordered_map>
#include <string>
#include <vector>

namespace vh::fs::model {
    struct File;
}

namespace vh::sync::model {
    struct Conflict;
}

namespace vh::storage {
    class CloudEngine;
}

namespace vh::sync {
    struct Cloud final : Local {
        std::vector<std::shared_ptr<fs::model::File> > localFiles, s3Files;
        std::unordered_map<std::u8string, std::shared_ptr<fs::model::File> > localMap, s3Map;
        std::unordered_map<std::u8string, std::optional<std::string> > remoteHashMap;

        ~Cloud() override = default;

        explicit Cloud(const std::shared_ptr<storage::Engine> &engine) : Local(engine) {
        }


        // ##########################################
        // ########### FSTask Overrides #############
        // ##########################################

        void operator()() override;


        // ##########################################
        // ############# Sync Operations ############
        // ##########################################

        void sync();

        void initBins();

        void clearBins();


        // ##########################################
        // ########### File Operations #############
        // ##########################################

        void upload(const std::shared_ptr<fs::model::File> &file);

        void download(const std::shared_ptr<fs::model::File> &file, bool freeAfterDownload = false);

        void remove(const std::shared_ptr<fs::model::File> &file,
                    const tasks::Delete::Type &type = tasks::Delete::Type::PURGE);


        // ##########################################
        // ############ Internal Helpers ############
        // ##########################################

        std::shared_ptr<storage::CloudEngine> cloudEngine() const;

        std::vector<model::EntryKey> allKeysSorted() const;

        void ensureDirectoriesFromRemote();


        // ##########################################
        // ########### Conflict Handling ############
        // ##########################################

        [[nodiscard]] static bool hasPotentialConflict(const std::shared_ptr<fs::model::File> &local,
                                                       const std::shared_ptr<fs::model::File> &upstream,
                                                       bool upstream_decryption_failure);

        std::shared_ptr<model::Conflict> maybeBuildConflict(const std::shared_ptr<fs::model::File> &local,
                                                            const std::shared_ptr<fs::model::File> &upstream) const;

        bool handleConflict(const std::shared_ptr<model::Conflict> &c) const;


        // ##########################################
        // ########### Static Helpers ###############
        // ##########################################

        static uintmax_t computeReqFreeSpaceForDownload(const std::vector<std::shared_ptr<fs::model::File> > &files);

        static std::vector<std::shared_ptr<fs::model::File> > uMap2Vector(
            std::unordered_map<std::u8string, std::shared_ptr<fs::model::File> > &map);

        static std::unordered_map<std::u8string, std::shared_ptr<fs::model::File> > intersect(
            const std::unordered_map<std::u8string, std::shared_ptr<fs::model::File> > &a,
            const std::unordered_map<std::u8string, std::shared_ptr<fs::model::File> > &b);
    };
}