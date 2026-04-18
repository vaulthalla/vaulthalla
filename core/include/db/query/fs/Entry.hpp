#pragma once

#include <filesystem>
#include <memory>
#include <vector>
#include <optional>
#include <pqxx/result>

namespace fs = std::filesystem;

namespace vh::fs::model { struct Entry; }

namespace vh::db::query::fs {

class Entry {
    using FSEntry = vh::fs::model::Entry;
    using EntryPtr = std::shared_ptr<FSEntry>;

public:
    static void updateFSEntry(const EntryPtr& entry);

    static EntryPtr getFSEntry(const std::string& base32);

    static EntryPtr getFSEntryByInode(ino_t ino);

    static EntryPtr getFSEntryById(unsigned int entryId);

    static std::vector<EntryPtr> listDir(const std::optional<unsigned int>& entryId, bool recursive = false);

    static void renameEntry(const EntryPtr& entry);

    [[nodiscard]] static ino_t getNextInode();

    [[nodiscard]] static bool rootExists();

    [[nodiscard]] static EntryPtr getRootEntry();

    [[nodiscard]] static pqxx::result collectParentChain(unsigned int parentId);
};

}
