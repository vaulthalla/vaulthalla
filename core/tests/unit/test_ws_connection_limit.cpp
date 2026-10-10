// websocket_server.max_connections (#164): the setting used to be parsed and never read. ws::Server now takes a slot
// per accepted connection and refuses the rest with a plain 503; the session gives its slot back when it closes.

#include "protocols/ws/ConnectionLimit.hpp"
#include "protocols/ws/Router.hpp"
#include "protocols/ws/Session.hpp"

#include <gtest/gtest.h>

#include <boost/asio.hpp>

#include <memory>
#include <string>
#include <vector>

namespace vh::test_ws_connection_limit {

using protocols::ws::ConnectionSlot;
namespace asio = boost::asio;
using tcp = asio::ip::tcp;

TEST(WsConnectionLimit, SlotsStopAtTheCapAndComeBackWhenReleased) {
    const auto base = ConnectionSlot::active();
    const auto refusedBefore = ConnectionSlot::refused();
    const auto cap = base + 2;

    auto first = ConnectionSlot::tryAcquire(cap);
    auto second = ConnectionSlot::tryAcquire(cap);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_EQ(ConnectionSlot::active(), base + 2);
    EXPECT_FALSE(ConnectionSlot::tryAcquire(cap));
    EXPECT_EQ(ConnectionSlot::refused(), refusedBefore + 1);

    first.reset();
    EXPECT_EQ(ConnectionSlot::active(), base + 1);
    auto third = ConnectionSlot::tryAcquire(cap);
    EXPECT_TRUE(third);
    third.reset();
    second.reset();
    EXPECT_EQ(ConnectionSlot::active(), base);
}

TEST(WsConnectionLimit, ZeroIsNotUnlimited) {
    if (ConnectionSlot::active() != 0) GTEST_SKIP() << "another test holds a slot";
    auto only = ConnectionSlot::tryAcquire(0);
    ASSERT_TRUE(only);
    EXPECT_FALSE(ConnectionSlot::tryAcquire(0));
}

TEST(WsConnectionLimit, SessionReleasesItsSlotOnCloseOrDestruction) {
    const auto base = ConnectionSlot::active();
    {
        auto session = std::make_shared<protocols::ws::Session>(std::make_shared<protocols::ws::Router>());
        session->holdConnectionSlot(ConnectionSlot::tryAcquire(base + 1));
        EXPECT_EQ(ConnectionSlot::active(), base + 1);
        session->close();
        EXPECT_EQ(ConnectionSlot::active(), base) << "close() must give the slot back at once";
        session->close();
        EXPECT_EQ(ConnectionSlot::active(), base);
    }
    {
        auto session = std::make_shared<protocols::ws::Session>(std::make_shared<protocols::ws::Router>());
        session->holdConnectionSlot(ConnectionSlot::tryAcquire(base + 1));
        EXPECT_EQ(ConnectionSlot::active(), base + 1);
    }
    EXPECT_EQ(ConnectionSlot::active(), base);
}

TEST(WsConnectionLimit, RefusalIsAPlain503WithRetryAfter) {
    asio::io_context ioc;
    tcp::acceptor acceptor(ioc, tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0));
    tcp::socket client(ioc);
    client.connect(acceptor.local_endpoint());
    tcp::socket server(ioc);
    acceptor.accept(server);

    protocols::ws::refuseOverCapacity(server);
    EXPECT_FALSE(server.is_open());

    std::string received;
    std::vector<char> buffer(512);
    boost::system::error_code ec;
    while (!ec) {
        const auto n = client.read_some(asio::buffer(buffer), ec);
        received.append(buffer.data(), n);
    }
    EXPECT_EQ(ec, asio::error::eof);
    EXPECT_TRUE(received.starts_with("HTTP/1.1 503 Service Unavailable\r\n")) << received;
    EXPECT_NE(received.find("Retry-After: 1\r\n"), std::string::npos);
    EXPECT_NE(received.find("Connection: close\r\n"), std::string::npos);
}

}
