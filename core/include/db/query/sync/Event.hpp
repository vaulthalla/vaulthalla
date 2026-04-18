#pragma once

#include <memory>
#include <vector>

namespace vh::sync::model { struct Event; }

namespace vh::db::query::sync {

class Event {
    using E = vh::sync::model::Event;

public:
    using EventPtr = std::shared_ptr<E>;

    static void create(const EventPtr& event);
    static void upsert(const EventPtr& event);
    static EventPtr getLatest(unsigned int vaultId);
    static std::vector<EventPtr> getEvents(unsigned int vaultId, unsigned int limit, unsigned int offset);
    static std::vector<EventPtr> getEvents(unsigned int vaultId);
    static void heartbeat(const EventPtr& event);

    static void purgeOld();
};

}
