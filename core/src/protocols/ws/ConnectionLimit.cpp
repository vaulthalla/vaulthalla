#include "protocols/ws/ConnectionLimit.hpp"

#include <boost/asio/buffer.hpp>
#include <boost/asio/write.hpp>

#include <algorithm>
#include <atomic>
#include <string_view>

namespace vh::protocols::ws {

namespace {
std::atomic<unsigned int> activeSlots{0};
std::atomic<uint64_t> refusedConnections{0};
}

std::unique_ptr<ConnectionSlot> ConnectionSlot::tryAcquire(const unsigned int limit) {
    const auto cap = std::max(1u, limit);
    auto current = activeSlots.load(std::memory_order_relaxed);
    do {
        if (current >= cap) {
            refusedConnections.fetch_add(1, std::memory_order_relaxed);
            return nullptr;
        }
    } while (!activeSlots.compare_exchange_weak(current, current + 1, std::memory_order_acq_rel,
                                                std::memory_order_relaxed));
    return std::unique_ptr<ConnectionSlot>(new ConnectionSlot());
}

unsigned int ConnectionSlot::active() noexcept { return activeSlots.load(std::memory_order_relaxed); }

uint64_t ConnectionSlot::refused() noexcept { return refusedConnections.load(std::memory_order_relaxed); }

ConnectionSlot::~ConnectionSlot() { activeSlots.fetch_sub(1, std::memory_order_acq_rel); }

void refuseOverCapacity(boost::asio::ip::tcp::socket& socket) {
    static constexpr std::string_view kBusy =
        "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nRetry-After: 1\r\nConnection: close\r\n\r\n";
    boost::system::error_code ec;
    // A fresh socket's send buffer takes these few bytes at once, so the blocking write can't stall the acceptor.
    boost::asio::write(socket, boost::asio::buffer(kBusy.data(), kBusy.size()), ec);
    socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
    socket.close(ec);
}

}
