// Socket-level HTTP session behavior: streamed bodies, client disconnects stopping work, stalled clients timing out,
// idle keep-alive deadlines and HEAD framing.
#include "protocols/http/Router.hpp"
#include "protocols/http/Session.hpp"
#include "storage/PlaintextReader.hpp"

#include <boost/asio.hpp>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>

namespace vh::protocols::http::test_session {

using namespace std::chrono_literals;
namespace asio = boost::asio;
using tcp = asio::ip::tcp;

namespace {

class PatternReader final : public storage::PlaintextReader {
public:
    explicit PatternReader(uint64_t size) : size_(size) { generation_.size = size; }
    [[nodiscard]] uint64_t size() const override { return size_; }
    std::size_t read(const uint64_t offset, std::span<uint8_t> out) override {
        if (offset >= size_) return 0;
        const auto n = static_cast<std::size_t>(std::min<uint64_t>(out.size(), size_ - offset));
        for (std::size_t i = 0; i < n; ++i) out[i] = static_cast<uint8_t>((offset + i) * 31u);
        reads.fetch_add(1);
        return n;
    }
    [[nodiscard]] const storage::Generation& generation() const override { return generation_; }
    std::atomic<uint64_t> reads{0};

private:
    uint64_t size_;
    storage::Generation generation_;
};

struct Finish {
    std::promise<std::pair<uint64_t, bool>> promise;
};

struct Harness {
    asio::io_context ioc;
    tcp::acceptor acceptor{ioc, tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0)};
    tcp::socket client{ioc};
    std::thread serverThread;
    std::atomic<bool> sessionEnded{false};

    void start() {
        client.connect(acceptor.local_endpoint());
        tcp::socket server(ioc);
        acceptor.accept(server);
        auto session = Session::open(std::move(server));
        serverThread = std::thread([this, session] {
            session->run();
            sessionEnded = true;
        });
    }

    ~Harness() {
        boost::system::error_code ec;
        client.close(ec);
        if (serverThread.joinable()) serverThread.join();
    }

    void send(const std::string& raw) { asio::write(client, asio::buffer(raw)); }

    std::string readHeaders() {
        asio::streambuf buf;
        asio::read_until(client, buf, "\r\n\r\n");
        std::string all{asio::buffers_begin(buf.data()), asio::buffers_end(buf.data())};
        leftover = all.substr(all.find("\r\n\r\n") + 4);
        return all.substr(0, all.find("\r\n\r\n") + 4);
    }
    std::string leftover;
};

model::preview::StreamResponse streamOf(const std::shared_ptr<PatternReader>& reader, Finish* finish) {
    model::preview::StreamResponse res;
    res.result(boost::beast::http::status::ok);
    res.version(11);
    res.set(boost::beast::http::field::content_type, "application/octet-stream");
    res.reader = reader;
    res.length = reader->size();
    if (finish)
        res.onFinish = [finish](const uint64_t sent, const bool complete) {
            finish->promise.set_value({sent, complete});
        };
    return res;
}

}

class HttpSessionTest : public ::testing::Test {
protected:
    void TearDown() override {
        Router::setRouteOverrideForTesting({});
        Session::setTimeoutsForTesting(20s, 60s);
    }
};

TEST_F(HttpSessionTest, StreamsTheWholeBodyWithExactFraming) {
    const auto reader = std::make_shared<PatternReader>(8ull << 20);
    Finish finish;
    Router::setRouteOverrideForTesting([&](request&&) -> model::preview::Response { return streamOf(reader, &finish); });
    Harness h;
    h.start();
    h.send("GET /x HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n");
    const auto headers = h.readHeaders();
    EXPECT_NE(headers.find("Content-Length: 8388608"), std::string::npos);
    std::string body = h.leftover;
    boost::system::error_code ec;
    std::array<char, 65536> buf{};
    while (true) {
        const auto n = h.client.read_some(asio::buffer(buf), ec);
        if (ec) break;
        body.append(buf.data(), n);
    }
    ASSERT_EQ(body.size(), 8ull << 20);
    bool pattern = true;
    for (std::size_t i = 0; i < body.size(); i += 4097) pattern &= static_cast<uint8_t>(body[i]) == static_cast<uint8_t>(i * 31u);
    EXPECT_TRUE(pattern);
    const auto [sent, complete] = finish.promise.get_future().get();
    EXPECT_EQ(sent, 8ull << 20);
    EXPECT_TRUE(complete);
}

TEST_F(HttpSessionTest, ClientDisconnectStopsReadingPromptly) {
    const auto reader = std::make_shared<PatternReader>(1ull << 34);  // 16 GiB: never fully read
    Finish finish;
    Router::setRouteOverrideForTesting([&](request&&) -> model::preview::Response { return streamOf(reader, &finish); });
    auto h = std::make_unique<Harness>();
    h->start();
    h->send("GET /x HTTP/1.1\r\nHost: t\r\n\r\n");
    (void)h->readHeaders();
    std::array<char, 1 << 20> buf{};
    boost::system::error_code ec;
    (void)asio::read(h->client, asio::buffer(buf), ec);
    h->client.close(ec);

    auto fut = finish.promise.get_future();
    ASSERT_EQ(fut.wait_for(10s), std::future_status::ready);
    const auto [sent, complete] = fut.get();
    EXPECT_FALSE(complete);
    EXPECT_LT(sent, 1ull << 30);
    const auto readsAtFinish = reader->reads.load();
    std::this_thread::sleep_for(200ms);
    EXPECT_EQ(reader->reads.load(), readsAtFinish);  // no further decrypt work after the client left
}

TEST_F(HttpSessionTest, StalledClientIsDroppedAfterTheWriteDeadline) {
    Session::setTimeoutsForTesting(5s, 300ms);
    const auto reader = std::make_shared<PatternReader>(1ull << 34);
    Finish finish;
    Router::setRouteOverrideForTesting([&](request&&) -> model::preview::Response { return streamOf(reader, &finish); });
    Harness h;
    h.start();
    h.send("GET /x HTTP/1.1\r\nHost: t\r\n\r\n");
    // Never read: socket buffers fill, writes stall, the deadline fires.
    auto fut = finish.promise.get_future();
    ASSERT_EQ(fut.wait_for(15s), std::future_status::ready);
    EXPECT_FALSE(fut.get().second);
}

TEST_F(HttpSessionTest, IdleKeepAliveConnectionsAreClosed) {
    Session::setTimeoutsForTesting(300ms, 5s);
    Router::setRouteOverrideForTesting([](request&& req) { return Router::makeJsonResponse(req, nlohmann::json{{"ok", true}}); });
    Harness h;
    h.start();
    const auto started = std::chrono::steady_clock::now();
    while (!h.sessionEnded && std::chrono::steady_clock::now() - started < 10s) std::this_thread::sleep_for(20ms);
    EXPECT_TRUE(h.sessionEnded.load());
}

TEST_F(HttpSessionTest, HeadSendsHeadersWithTheGetContentLengthAndNoBody) {
    Router::setRouteOverrideForTesting([](request&& req) {
        std::vector<uint8_t> data(1234, 'x');
        return Router::makeResponse(req, std::move(data), "image/jpeg");
    });
    Harness h;
    h.start();
    h.send("HEAD /x HTTP/1.1\r\nHost: t\r\n\r\nGET /y HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n");
    const auto head = h.readHeaders();
    EXPECT_NE(head.find("Content-Length: 1234"), std::string::npos);
    // The next bytes on the wire are the second response, not a HEAD body.
    std::string rest = h.leftover;
    boost::system::error_code ec;
    std::array<char, 4096> buf{};
    while (true) {
        const auto n = h.client.read_some(asio::buffer(buf), ec);
        if (ec) break;
        rest.append(buf.data(), n);
    }
    EXPECT_EQ(rest.rfind("HTTP/1.1 200", 0), 0u);
    EXPECT_EQ(rest.size() - rest.find("\r\n\r\n") - 4, 1234u);
}

}
