#ifndef WEBSOCKET_CLIENT_H
#define WEBSOCKET_CLIENT_H

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/system/error_code.hpp>

#include <functional>
#include <string>

namespace NetworkMonitor {

    /*! \brief Client to connect to a WebSocket server over plain TCP.
     */
    class WebSocketClient {
    public:
        /*! \brief Construct a WebSocket client.
         *
         *  \note This constructor does not initiate a connection.
         *
         *  \param url      The URL of the server.
         *  \param endpoint The endpoint on the server to connect to.
         *                  Example: ltnm.learncppthroughprojects.com/<endpoint>
         *  \param port     The port on the server.
         *  \param ioc      The io_context object. The user takes care of calling
         *                  ioc.run().
         */
        WebSocketClient(
            const std::string& url,
            const std::string& endpoint,
            const std::string& port,
            boost::asio::io_context& ioc
        );

        /*! \brief Destructor.
         */
        ~WebSocketClient();

        /*! \brief Connect to the server.
         *
         *  \param onConnect     Called when the connection fails or succeeds.
         *  \param onMessage     Called only when a message is successfully
         *                       received. The message is an rvalue reference;
         *                       ownership is passed to the receiver.
         *  \param onDisconnect  Called when the connection is closed by the server
         *                       or due to a connection error.
         */
        void Connect(
            std::function<void(boost::system::error_code)> onConnect = nullptr,
            std::function<void(boost::system::error_code,
                std::string&&)> onMessage = nullptr,
            std::function<void(boost::system::error_code)> onDisconnect = nullptr
        );

        /*! \brief Send a text message to the WebSocket server.
         *
         *  \param message The message to send. The caller must ensure that this
         *                 string stays in scope until the onSend handler is called.
         *  \param onSend  Called when a message is sent successfully or if it
         *                 failed to send.
         */
        void Send(
            const std::string& message,
            std::function<void(boost::system::error_code)> onSend = nullptr
        );

        /*! \brief Close the WebSocket connection.
         *
         *  \param onClose Called when the connection is closed, successfully or
         *                 not.
         */
        void Close(
            std::function<void(boost::system::error_code)> onClose = nullptr
        );

    private:
        std::string url_{};
        std::string endpoint_{};
        std::string port_{};

        // We leave these uninitialized because they do not support a default
        // constructor.
        boost::asio::ip::tcp::resolver resolver_;
        boost::beast::websocket::stream<boost::beast::tcp_stream> ws_;

        boost::beast::flat_buffer rBuffer_{};

        bool closed_{ true };

        std::function<void(boost::system::error_code)> onConnect_{ nullptr };
        std::function<void(boost::system::error_code,
            std::string&&)> onMessage_{ nullptr };
        std::function<void(boost::system::error_code)> onDisconnect_{ nullptr };

        void OnResolve(
            const boost::system::error_code& ec,
            boost::asio::ip::tcp::resolver::iterator resolverIt
        );

        void OnConnect(
            const boost::system::error_code& ec
        );

        void OnHandshake(
            const boost::system::error_code& ec
        );

        void ListenToIncomingMessage(
            const boost::system::error_code& ec
        );

        void OnRead(
            const boost::system::error_code& ec,
            size_t nBytes
        );
    };

} // namespace NetworkMonitor

#endif // WEBSOCKET_CLIENT_H