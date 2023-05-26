#include <network-monitor/websocket-client.h>

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/system/error_code.hpp>

#include <chrono>
#include <functional>
#include <iomanip>
#include <iostream>
#include <string>

using NetworkMonitor::WebSocketClient;

using tcp = boost::asio::ip::tcp;
namespace websocket = boost::beast::websocket;

// Static functions

static void Log(const std::string& where, boost::system::error_code ec)
{
    std::cerr << "[" << std::setw(20) << where << "] "
        << (ec ? "Error: " : "OK")
        << (ec ? ec.message() : "")
        << std::endl;
}

// Public methods

WebSocketClient::WebSocketClient(
    const std::string& url,
    const std::string& endpoint,
    const std::string& port,
    boost::asio::io_context& ioc
) : url_{ url },
endpoint_{ endpoint },
port_{ port },
resolver_{ boost::asio::make_strand(ioc) },
ws_{ boost::asio::make_strand(ioc) }
{
}

WebSocketClient::~WebSocketClient() = default;

void WebSocketClient::Connect(
    std::function<void(boost::system::error_code)> onConnect,
    std::function<void(boost::system::error_code,
        std::string&&)> onMessage,
    std::function<void(boost::system::error_code)> onDisconnect
)
{
    // Save the user callbacks for later use.
    onConnect_ = onConnect;
    onMessage_ = onMessage;
    onDisconnect_ = onDisconnect;

    // Start the chain of asynchronous callbacks.
    closed_ = false;
    resolver_.async_resolve(url_, port_,
        [this](auto ec, auto resolverIt) {
            OnResolve(ec, resolverIt);
        }
    );
}

void WebSocketClient::Send(
    const std::string& message,
    std::function<void(boost::system::error_code)> onSend
)
{
    ws_.async_write(boost::asio::buffer(message),
        [onSend](auto ec, auto) {
            if (onSend) {
                onSend(ec);
            }
        }
    );
}

void WebSocketClient::Close(
    std::function<void(boost::system::error_code)> onClose
)
{
    closed_ = true;
    ws_.async_close(
        websocket::close_code::none,
        [onClose](auto ec) {
            if (onClose) {
                onClose(ec);
            }
        }
    );
}

// Private methods

void WebSocketClient::OnResolve(
    const boost::system::error_code& ec,
    tcp::resolver::iterator resolverIt
)
{
    if (ec) {
        Log("OnResolve", ec);
        if (onConnect_) {
            onConnect_(ec);
        }
        return;
    }

    // The following timeout only matters for the purpose of connecting to the
    // TCP socket. We will reset the timeout to a sensible default after we are
    // connected.
    ws_.next_layer().expires_after(std::chrono::seconds(5));

    // Connect to the TCP socket.
    // Instead of constructing the socket and the ws objects separately, the
    // socket is now embedded in ws_, and we access it through next_layer().
    ws_.next_layer().async_connect(*resolverIt,
        [this](auto ec) {
            OnConnect(ec);
        }
    );
}

void WebSocketClient::OnConnect(
    const boost::system::error_code& ec
)
{
    if (ec) {
        Log("OnConnect", ec);
        if (onConnect_) {
            onConnect_(ec);
        }
        return;
    }

    // Now that the TCP socket is connected, we can reset the timeout to
    // whatever Boost.Beast recommends.
    ws_.next_layer().expires_never();
    ws_.set_option(websocket::stream_base::timeout::suggested(
        boost::beast::role_type::client
    ));

    // Attempt a WebSocket handshake.
    ws_.async_handshake(url_, endpoint_,
        [this](auto ec) {
            OnHandshake(ec);
        }
    );
}

void WebSocketClient::OnHandshake(
    const boost::system::error_code& ec
)
{
    if (ec) {
        Log("OnHandshake", ec);
        if (onConnect_) {
            onConnect_(ec);
        }
        return;
    }

    // Tell the WebSocket object to exchange messages in text format.
    ws_.text(true);

    // Now that we are connected, set up a recursive asynchronous listener to
    // receive messages.
    ListenToIncomingMessage(ec);

    // Dispatch the user callback.
    // Note: This call is synchronous and will block the WebSocket strand.
    if (onConnect_) {
        onConnect_(ec);
    }
}

void WebSocketClient::ListenToIncomingMessage(
    const boost::system::error_code& ec
)
{
    // Stop processing messages if the connection has been aborted.
    if (ec == boost::asio::error::operation_aborted) {
        // We check the closed_ flag to avoid notifying the user twice.
        if (onDisconnect_ && !closed_) {
            onDisconnect_(ec);
        }
        return;
    }

    // Read a message asynchronously. On a successful read, process the message
    // and recursively call this function again to process the next message.
    ws_.async_read(rBuffer_,
        [this](auto ec, auto nBytes) {
            OnRead(ec, nBytes);
            ListenToIncomingMessage(ec);
        }
    );
}

void WebSocketClient::OnRead(
    const boost::system::error_code& ec,
    size_t nBytes
)
{
    // We just ignore messages that failed to read.
    if (ec) {
        return;
    }

    // Parse the message and forward it to the user callback.
    // Note: This call is synchronous and will block the WebSocket strand.
    std::string message{ boost::beast::buffers_to_string(rBuffer_.data()) };
    rBuffer_.consume(nBytes);
    if (onMessage_) {
        onMessage_(ec, std::move(message));
    }
}