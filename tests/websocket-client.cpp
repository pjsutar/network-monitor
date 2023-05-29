#include <network-monitor/websocket-client.h>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/test/unit_test.hpp>

#include <filesystem>
#include <string>

using NetworkMonitor::WebSocketClient;

BOOST_AUTO_TEST_SUITE(network_monitor);

BOOST_AUTO_TEST_CASE(cacert_pem)
{
    BOOST_CHECK(std::filesystem::exists(TESTS_CACERT_PEM));
}

BOOST_AUTO_TEST_CASE(class_WebSocketClient)
{
    // Connection targets
    const std::string url{ "ltnm.learncppthroughprojects.com" };
    const std::string endpoint{ "/echo" };
    const std::string port{ "443" };
    const std::string message{ "Hello WebSocket" };

    // TLS context
    boost::asio::ssl::context ctx{ boost::asio::ssl::context::tlsv12_client };
    ctx.load_verify_file(TESTS_CACERT_PEM);

    // Always start with an I/O context object.
    boost::asio::io_context ioc{};

    // The class under test
    WebSocketClient client{ url, endpoint, port, ioc, ctx };

    // We use these flags to check that the connection, send, receive functions
    // work as expected.
    bool connected{ false };
    bool messageSent{ false };
    bool messageReceived{ false };
    bool disconnected{ false };
    std::string echo{};

    // Our own callbacks
    auto onSend{ [&messageSent](auto ec) {
        messageSent = !ec;
    } };
    auto onConnect{ [&client, &connected, &onSend, &message](auto ec) {
        connected = !ec;
        if (!ec) {
            client.Send(message, onSend);
        }
    } };
    auto onClose{ [&disconnected](auto ec) {
        disconnected = !ec;
    } };
    auto onReceive{ [&client,
                      &onClose,
                      &messageReceived,
                      &echo](auto ec, auto received) {
        messageReceived = !ec;
        echo = std::move(received);
        client.Close(onClose);
    } };

    // We must call io_context::run for asynchronous callbacks to run.
    client.Connect(onConnect, onReceive);
    ioc.run();

    // When we get here, the io_context::run function has run out of work to do.
    BOOST_CHECK(connected);
    BOOST_CHECK(messageSent);
    BOOST_CHECK(messageReceived);
    BOOST_CHECK(disconnected);
    BOOST_CHECK_EQUAL(message, echo);
}

BOOST_AUTO_TEST_SUITE_END();