#ifndef NETWORK_MONITOR_TESTS_WEBSOCKET_SERVER_MOCK_H
#define NETWORK_MONITOR_TESTS_WEBSOCKET_SERVER_MOCK_H

#include <network-monitor/stomp-frame.h>
#include <network-monitor/websocket-server.h>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <queue>
#include <string>
#include <utility>

namespace NetworkMonitor {

    /*! \brief Craft a mock STOMP frame.
     */
    std::string GetMockStompFrame(
        const std::string& host
    );

    /*! \brief Craft a mock SEND frame.
     */
    std::string GetMockSendFrame(
        const std::string& id,
        const std::string& destination,
        const std::string& payload
    );

    /*! \brief Mock a WebSocket event.
     *
     *  A test can specify a sequence of mock events for a given WebSocket session.
     */
    struct MockWebSocketEvent {
        /*! \brief The WebSocket event type.
         */
        enum class Type {
            kConnect,
            kMessage,
            kDisconnect,
        };

        std::string id{};
        Type type{ Type::kConnect };
        boost::system::error_code ec{};
        std::string message{};
    };

    /*! \brief Mock the WebSocketServer class.
     *
     *  We do not mock all available methods — only the ones we are interested in
     *  for testing.
     */
    class MockWebSocketSession : public std::enable_shared_from_this<
        MockWebSocketSession
    > {
    public:
        // Use these static members in a test to set the error codes returned by
        // the mock.
        static boost::system::error_code sendEc;

        /*! \brief Mock handler type for MockWebSocketSession.
         */
        using Handler = std::function<
            void(
                boost::system::error_code,
                std::shared_ptr<MockWebSocketSession>
                )
        >;

        /*! \brief Mock message handler type for MockWebSocketSession.
         */
        using MsgHandler = std::function<
            void (
                boost::system::error_code,
                std::shared_ptr<MockWebSocketSession>,
                std::string&&
                )
        >;

        /*! \brief Mock constructor. This is the only method that has a different
         *         signature from the original one.
         */
        MockWebSocketSession(
            boost::asio::io_context& ioc
        );

        /*! \brief Mock destructor.
         */
        ~MockWebSocketSession();

        /*! \brief Send a mock message.
         */
        void Send(
            const std::string& message,
            std::function<void(boost::system::error_code)> onSend = nullptr
        );

        /*! \brief Mock close.
         */
        void Close(
            std::function<void(boost::system::error_code)> onClose = nullptr
        );

    private:
        // This strand handles all the user callbacks.
        // We leave it uninitialized because it does not support a default
        // constructor.
        boost::asio::strand<boost::asio::io_context::executor_type> context_;

        bool connected_{ false };

        void MockIncomingMessages(
            std::function<void(boost::system::error_code,
                std::string&&)> onMessage = nullptr,
            std::function<void(boost::system::error_code)> onDisconnect = nullptr
        );
    };

    /*! \brief Mock the WebSocketServer class.
     *
     *  We do not mock all available methods — only the ones we are interested in
     *  for testing.
     */
    class MockWebSocketServer {
    public:
        // Use these static members in a test to set the error codes returned by
        // the mock.
        static bool triggerDisconnection;
        static boost::system::error_code runEc;
        static std::queue<MockWebSocketEvent> mockEvents;

        /*! \brief Mock session type.
         */
        using Session = MockWebSocketSession;

        /*! \brief Mock constructor.
         */
        MockWebSocketServer(
            const std::string& ip,
            const unsigned short port,
            boost::asio::io_context& ioc,
            boost::asio::ssl::context& ctx
        );

        /*! \brief Mock destructor.
         */
        virtual ~MockWebSocketServer();

        /*! \brief Mock run.
         */
        boost::system::error_code Run(
            Session::Handler onSessionConnect = nullptr,
            Session::MsgHandler onSessionMessage = nullptr,
            Session::Handler onSessionDisconnect = nullptr,
            std::function<void(boost::system::error_code)> onDisconnect = nullptr
        );

        /*! \brief Mock stop.
         */
        void Stop();

    private:
        // We leave it uninitialized because it does not support a default
        // constructor.
        boost::asio::io_context& ioc_;

        // This strand handles all the user callbacks.
        boost::asio::strand<boost::asio::io_context::executor_type> context_;

        std::unordered_map<
            std::string,
            std::shared_ptr<MockWebSocketSession>
        > connections_{};

        bool started_{ false };
        bool stopped_{ false };

        void ListenToMockConnections(
            Session::Handler onSessionConnect,
            Session::MsgHandler onSessionMessage,
            Session::Handler onSessionDisconnect,
            std::function<void(boost::system::error_code)> onDisconnect
        );
    };

    /*! \brief Mock the WebSocketServer class to connect to a STOMP client.
     *
     *  We do not mock all available methods — only the ones we are interested in
     *  for testing.
     */
    class MockWebSocketServerForStomp : public MockWebSocketServer {
        using MockWebSocketServer::MockWebSocketServer;
    };

} // namespace NetworkMonitor

#endif // NETWORK_MONITOR_TESTS_BOOST_MOCK_H