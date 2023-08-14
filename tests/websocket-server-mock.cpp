#include "websocket-server-mock.h"

#include <network-monitor/stomp-frame.h>
#include <network-monitor/websocket-server.h>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include <nlohmann/json.hpp>

#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>

#include <functional>
#include <queue>
#include <stdexcept>
#include <string>
#include <vector>

using NetworkMonitor::MockWebSocketEvent;
using NetworkMonitor::MockWebSocketServer;
using NetworkMonitor::MockWebSocketServerForStomp;
using NetworkMonitor::MockWebSocketSession;
using NetworkMonitor::StompFrame;

// Free functions

std::string NetworkMonitor::GetMockStompFrame(
    const std::string& host
)
{
    StompError error;
    StompFrame frame{
        error,
        StompCommand::kStomp,
        {
            {StompHeader::kAcceptVersion, "1.2"},
            {StompHeader::kHost, host},
        }
    };
    if (error != StompError::kOk) {
        throw std::runtime_error("Unexpected: Invalid mock STOMP frame: " +
            ToString(error));
    }
    return frame.ToString();
}

std::string NetworkMonitor::GetMockSendFrame(
    const std::string& id,
    const std::string& destination,
    const std::string& payload
)
{
    StompError error;
    StompFrame frame{
        error,
        StompCommand::kSend,
        {
            {StompHeader::kId, id},
            {StompHeader::kDestination, destination},
            {StompHeader::kContentType, "application/json"},
            {StompHeader::kContentLength, std::to_string(payload.size())},
        },
        payload
    };
    if (error != StompError::kOk) {
        throw std::runtime_error("Unexpected: Invalid mock STOMP frame: " +
            ToString(error));
    }
    return frame.ToString();
}

// MockWebSocketSession

// Static member variables definition.
boost::system::error_code MockWebSocketSession::sendEc = {};

MockWebSocketSession::MockWebSocketSession(
    boost::asio::io_context& ioc
) : context_{ boost::asio::make_strand(ioc) }
{
    // We don't need to save anything apart from the strand.
}

MockWebSocketSession::~MockWebSocketSession() = default;

void MockWebSocketSession::Send(
    const std::string& message,
    std::function<void(boost::system::error_code)> onSend
)
{
    spdlog::info("MockWebSocketSession::Send");
    if (onSend) {
        boost::asio::post(
            context_,
            [onSend]() {
                onSend(sendEc);
            }
        );
    }
}

void MockWebSocketSession::Close(
    std::function<void(boost::system::error_code)> onClose
)
{
    spdlog::info("MockWebSocketSession::Close");
    if (onClose) {
        boost::asio::post(
            context_,
            [onClose]() {
                onClose({});
            }
        );
    }
}

// MockWebSocketServer

// Static member variables definition.
bool MockWebSocketServer::triggerDisconnection = false;
boost::system::error_code MockWebSocketServer::runEc = {};
std::queue<MockWebSocketEvent> MockWebSocketServer::mockEvents = {};

MockWebSocketServer::MockWebSocketServer(
    const std::string& ip,
    const unsigned short port,
    boost::asio::io_context& ioc,
    boost::asio::ssl::context& ctx
) : ioc_(ioc), context_{ boost::asio::make_strand(ioc) }
{
    // We don't need to save anything apart from the strand.
}

MockWebSocketServer::~MockWebSocketServer() = default;

boost::system::error_code MockWebSocketServer::Run(
    MockWebSocketServer::Session::Handler onSessionConnect,
    MockWebSocketServer::Session::MsgHandler onSessionMessage,
    MockWebSocketServer::Session::Handler onSessionDisconnect,
    std::function<void(boost::system::error_code)> onDisconnect
)
{
    if (!runEc) {
        started_ = true;
        stopped_ = false;
        ListenToMockConnections(
            onSessionConnect,
            onSessionMessage,
            onSessionDisconnect,
            onDisconnect
        );
    }
    return runEc;
}

void MockWebSocketServer::Stop()
{
    stopped_ = true;
}

void MockWebSocketServer::ListenToMockConnections(
    MockWebSocketServer::Session::Handler onSessionConnect,
    MockWebSocketServer::Session::MsgHandler onSessionMessage,
    MockWebSocketServer::Session::Handler onSessionDisconnect,
    std::function<void(boost::system::error_code)> onDisconnect
)
{
    if (!started_ || stopped_ || triggerDisconnection) {
        triggerDisconnection = false;
        boost::asio::post(
            context_,
            [onDisconnect, stopped = stopped_]() {
                if (onDisconnect && !stopped) {
                    onDisconnect(boost::asio::error::operation_aborted);
                }
            }
        );
        return;
    }

    // Process one event.
    if (mockEvents.size() > 0) {
        auto event{ mockEvents.front() };
        mockEvents.pop();
        switch (event.type) {
        case MockWebSocketEvent::Type::kConnect: {
            auto connection{ std::make_shared<MockWebSocketSession>(ioc_) };
            connections_[event.id] = connection;
            if (onSessionConnect) {
                boost::asio::post(
                    context_,
                    [onSessionConnect, ec = event.ec, connection]() {
                        onSessionConnect(ec, connection);
                    }
                );
            }
            break;
        }
        case MockWebSocketEvent::Type::kMessage: {
            auto connectionIt{ connections_.find(event.id) };
            if (connectionIt == connections_.end()) {
                throw std::runtime_error(
                    "MockWebSocketSession: Invalid connection " + event.id
                );
            }
            const auto& connection{ connectionIt->second };
            if (onSessionMessage) {
                boost::asio::post(
                    context_,
                    [onSessionMessage, ec = event.ec, msg = event.message,
                    connection]() mutable {
                        onSessionMessage(ec, connection, std::move(msg));
                    }
                );
            }
            break;
        }
        case MockWebSocketEvent::Type::kDisconnect: {
            auto connectionIt{ connections_.find(event.id) };
            if (connectionIt == connections_.end()) {
                throw std::runtime_error(
                    "MockWebSocketSession: Invalid connection " + event.id
                );
            }
            const auto& connection{ connectionIt->second };
            if (onSessionDisconnect) {
                boost::asio::post(
                    context_,
                    [onSessionDisconnect, ec = event.ec, connection]() {
                        onSessionDisconnect(ec, connection);
                    }
                );
            }
            break;
        }
        default:
            throw std::runtime_error(
                "MockWebSocketSession: Unknown mock event type"
            );
            break;
        }
    }

    // Recurse.
    boost::asio::post(
        context_,
        [
            this,
            onSessionConnect,
            onSessionMessage,
            onSessionDisconnect,
            onDisconnect
        ]() {
            ListenToMockConnections(
                onSessionConnect,
                onSessionMessage,
                onSessionDisconnect,
                onDisconnect
            );
        }
    );
}